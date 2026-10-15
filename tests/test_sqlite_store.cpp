#include "loomlog/sqlite_store.hpp"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path temp_db(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

void remove_db(const std::filesystem::path& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
}

std::vector<std::string> paths_of(const std::vector<loom::SqliteSearchHit>& hits) {
    std::vector<std::string> paths;
    paths.reserve(hits.size());
    for (const auto& hit : hits) {
        paths.push_back(hit.path);
    }
    return paths;
}

}  // namespace

TEST(SqliteStoreTest, InsertSearchAndCount) {
    const auto db = temp_db("loomlog_sqlite_store_basic.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.insert("documents/thread_pool.txt", "thread pool worker");
    store.insert("documents/logger.md", "thread logger");

    EXPECT_EQ(store.document_count(), 2);

    const auto hits = store.search("thread");
    ASSERT_EQ(hits.size(), 2);
    const auto paths = paths_of(hits);
    EXPECT_NE(std::find(paths.begin(), paths.end(), "documents/thread_pool.txt"), paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(), "documents/logger.md"), paths.end());

    const auto and_hits = store.search("thread pool");
    ASSERT_EQ(and_hits.size(), 1);
    EXPECT_EQ(and_hits[0].path, "documents/thread_pool.txt");
}

TEST(SqliteStoreTest, PersistsAcrossReopen) {
    const auto db = temp_db("loomlog_sqlite_store_persist.db");
    remove_db(db);

    {
        loom::SqliteStore store;
        store.open(db);
        store.insert("documents/a.txt", "alpha beta");
        store.close();
    }

    loom::SqliteStore store;
    store.open(db);
    EXPECT_EQ(store.document_count(), 1);
    const auto hits = store.search("alpha");
    ASSERT_EQ(hits.size(), 1);
    EXPECT_EQ(hits[0].path, "documents/a.txt");
}

TEST(SqliteStoreTest, PhraseQueryMatchesOnlyAdjacentWords) {
    const auto db = temp_db("loomlog_sqlite_store_phrase.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.insert("documents/thread_pool.txt", "thread pool worker");
    store.insert("documents/pool_thread.txt", "pool thread worker");

    const auto hits = store.search("\"thread pool\"");
    ASSERT_EQ(hits.size(), 1);
    EXPECT_EQ(hits[0].path, "documents/thread_pool.txt");
}

TEST(SqliteStoreTest, RanksByTermFrequency) {
    const auto db = temp_db("loomlog_sqlite_store_rank.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.insert("documents/frequent.txt", "thread pool thread pool thread pool");
    store.insert("documents/rare.txt", "thread pool");

    const auto hits = store.search("thread pool");
    ASSERT_EQ(hits.size(), 2);
    EXPECT_EQ(hits[0].path, "documents/frequent.txt");
    EXPECT_EQ(hits[1].path, "documents/rare.txt");
}

TEST(SqliteStoreTest, ReusesInsertAcrossTransactionsAndReopen) {
    const auto db = temp_db("loomlog_sqlite_store_reuse.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.begin_transaction();
    for (int i = 0; i < 70; ++i) {
        store.insert("doc_" + std::to_string(i), i % 2 == 0 ? "alpha" : "beta");
    }
    store.commit_transaction();
    EXPECT_EQ(store.document_count(), 70u);
    EXPECT_EQ(store.search("alpha").size(), 35u);

    store.close();
    store.open(db);
    store.insert("new.txt", "gamma");
    EXPECT_EQ(store.document_count(), 71u);
    ASSERT_EQ(store.search("gamma").size(), 1u);
}

TEST(SqliteStoreTest, MoveAssignmentReleasesOldConnectionAndKeepsInsertUsable) {
    const auto first_db = temp_db("loomlog_sqlite_store_move_first.db");
    const auto second_db = temp_db("loomlog_sqlite_store_move_second.db");
    remove_db(first_db);
    remove_db(second_db);

    loom::SqliteStore first;
    first.open(first_db);
    loom::SqliteStore second;
    second.open(second_db);
    second.insert("before.txt", "beta");
    first = std::move(second);
    first.insert("after.txt", "alpha");
    EXPECT_EQ(first.document_count(), 2u);
    EXPECT_EQ(first.search("alpha").size(), 1u);
}

TEST(SqliteStoreTest, PersistsFileStatesAcrossReopenAndUpsertsByPath) {
    const auto db = temp_db("loomlog_sqlite_store_file_states.db");
    remove_db(db);
    {
        loom::SqliteStore store;
        store.open(db);
        store.upsert_file_state({"a.txt", 10, 100, "hash-a"});
        store.upsert_file_state({"b.txt", 20, 200, "hash-b"});
        store.upsert_file_state({"a.txt", 11, 101, "hash-a2"});
    }
    loom::SqliteStore store;
    store.open(db);
    const auto states = store.file_states();
    ASSERT_EQ(states.size(), 2u);
    EXPECT_EQ(states[0].path, "a.txt");
    EXPECT_EQ(states[0].file_size, 11u);
    EXPECT_EQ(states[0].last_write_time, 101);
    EXPECT_EQ(states[0].content_hash, "hash-a2");
    EXPECT_EQ(states[1].path, "b.txt");
}

TEST(SqliteStoreTest, RemovesFileStateByPath) {
    const auto db = temp_db("loomlog_sqlite_store_remove_state.db");
    remove_db(db);
    loom::SqliteStore store;
    store.open(db);
    store.upsert_file_state({"a.txt", 1, 2, "hash"});
    store.remove_file_state("a.txt");
    EXPECT_TRUE(store.file_states().empty());
}

TEST(SqliteStoreTest, PersistsIndexRootAcrossReopen) {
    const auto db = temp_db("loomlog_sqlite_store_root.db");
    remove_db(db);
    {
        loom::SqliteStore store;
        store.open(db);
        EXPECT_FALSE(store.index_root().has_value());
        store.set_index_root("D:/documents");
    }
    loom::SqliteStore store;
    store.open(db);
    EXPECT_EQ(store.index_root(), std::optional<std::string>("D:/documents"));
}

TEST(SqliteStoreTest, UpdatesDocumentContentWithoutLeavingOldTerms) {
    const auto db = temp_db("loomlog_sqlite_store_update.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.insert("documents/a.txt", "old unique term");
    store.update("documents/a.txt", "new unique term");

    EXPECT_TRUE(store.search("old").empty());
    const auto hits = store.search("new");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].path, "documents/a.txt");
    EXPECT_EQ(store.document_count(), 1u);
}

TEST(SqliteStoreTest, RemovesDocumentFromSearchResults) {
    const auto db = temp_db("loomlog_sqlite_store_remove.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.insert("documents/a.txt", "alpha");
    store.insert("documents/b.txt", "alpha");
    store.remove("documents/a.txt");

    const auto hits = store.search("alpha");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].path, "documents/b.txt");
    store.remove("documents/a.txt");
    EXPECT_EQ(store.document_count(), 1u);
}

TEST(SqliteStoreTest, RollbackDiscardsUncommittedChanges) {
    const auto db = temp_db("loomlog_sqlite_store_rollback.db");
    remove_db(db);

    loom::SqliteStore store;
    store.open(db);
    store.begin_transaction();
    store.insert("doc.txt", "secret keyword");
    store.rollback_transaction();

    EXPECT_EQ(store.document_count(), 0u);
    EXPECT_TRUE(store.search("secret").empty());
}

TEST(SqliteStoreTest, TracksAndValidatesSchemaVersion) {
    const auto db = temp_db("loomlog_sqlite_store_version.db");
    remove_db(db);

    {
        loom::SqliteStore store;
        store.open(db);
        EXPECT_EQ(store.schema_version(), 1);
    }

    {
        // 重新打开继续保持版本号
        loom::SqliteStore store;
        store.open(db);
        EXPECT_EQ(store.schema_version(), 1);
    }
}

TEST(SqliteStoreTest, DetectsUncleanShutdownAndRecoversCleanState) {
    const auto db = temp_db("loomlog_sqlite_store_crash.db");
    remove_db(db);

    {
        loom::SqliteStore store;
        store.open(db);
        EXPECT_TRUE(store.is_clean_shutdown());

        // 模拟开始写入事务并记录 syncing 状态，随后未正常 commit 就崩溃关机
        store.set_build_status("syncing");
        EXPECT_FALSE(store.is_clean_shutdown());
    }

    {
        // 崩溃后重新打开，检测到非干净退出，执行自愈恢复
        loom::SqliteStore store;
        store.open(db);
        EXPECT_FALSE(store.is_clean_shutdown());
        EXPECT_EQ(store.build_status(), "syncing");

        EXPECT_TRUE(store.recover_clean_state());
        EXPECT_TRUE(store.is_clean_shutdown());
        EXPECT_EQ(store.build_status(), "ready");

        // 再次恢复返回 false（因为已经是 clean）
        EXPECT_FALSE(store.recover_clean_state());
    }
}
