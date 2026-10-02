#include "loomlog/sqlite_store.hpp"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
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
