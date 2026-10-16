#include "loomlog/continuous_indexer.hpp"
#include "loomlog/sqlite_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::filesystem::path fresh_dir(std::string_view name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

// 辅助等待：循环轮询直到断言条件成立或超时
template <typename Predicate>
bool poll_until(Predicate pred, std::chrono::milliseconds timeout = 2500ms) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (pred()) return true;
        std::this_thread::sleep_for(30ms);
    }
    return pred();
}

}  // namespace

TEST(ContinuousIndexerTest, AutoIndexesNewFiles) {
    const auto dir = fresh_dir("loomsearch_continuous_create");
    const auto db_path = dir / "index.db";

    loom::ContinuousIndexer indexer;
    loom::ContinuousIndexerOptions options;
    options.root = dir;
    options.database = db_path;
    options.debounce_delay = 50ms;

    ASSERT_TRUE(indexer.start(options));

    // 1. 在目录下创建新文件
    const auto file_path = dir / "first.txt";
    {
        std::ofstream file(file_path);
        file << "apple banana cherry";
    }

    // 等待后台自动处理
    bool found = poll_until([&]() {
        loom::SqliteStore store;
        store.open(db_path);
        return !store.search("banana").empty();
    });

    EXPECT_TRUE(found);
    indexer.stop();
}

TEST(ContinuousIndexerTest, AutoReindexesModifiedFiles) {
    const auto dir = fresh_dir("loomsearch_continuous_modify");
    const auto db_path = dir / "index.db";
    const auto file_path = dir / "article.md";

    {
        std::ofstream file(file_path);
        file << "initial draft content";
    }

    loom::ContinuousIndexer indexer;
    loom::ContinuousIndexerOptions options;
    options.root = dir;
    options.database = db_path;
    options.debounce_delay = 50ms;

    ASSERT_TRUE(indexer.start(options));

    // 验证初始状态可被检索
    {
        loom::SqliteStore store;
        store.open(db_path);
        EXPECT_FALSE(store.search("draft").empty());
    }

    // 2. 修改文件内容，替换关键词
    std::this_thread::sleep_for(100ms);
    {
        std::ofstream file(file_path, std::ios::trunc);
        file << "revised final publication";
    }

    bool updated = poll_until([&]() {
        loom::SqliteStore store;
        store.open(db_path);
        return store.search("draft").empty() && !store.search("publication").empty();
    });

    EXPECT_TRUE(updated);
    indexer.stop();
}

TEST(ContinuousIndexerTest, AutoRemovesDeletedFiles) {
    const auto dir = fresh_dir("loomsearch_continuous_delete");
    const auto db_path = dir / "index.db";
    const auto file_path = dir / "doomed.cpp";

    {
        std::ofstream file(file_path);
        file << "void doomed_function() {}";
    }

    loom::ContinuousIndexer indexer;
    loom::ContinuousIndexerOptions options;
    options.root = dir;
    options.database = db_path;
    options.debounce_delay = 50ms;

    ASSERT_TRUE(indexer.start(options));

    {
        loom::SqliteStore store;
        store.open(db_path);
        EXPECT_EQ(store.document_count(), 1u);
    }

    // 3. 删除文件
    std::this_thread::sleep_for(100ms);
    std::filesystem::remove(file_path);

    bool removed = poll_until([&]() {
        loom::SqliteStore store;
        store.open(db_path);
        return store.document_count() == 0u && store.search("doomed").empty();
    });

    EXPECT_TRUE(removed);
    indexer.stop();
}

TEST(ContinuousIndexerTest, CollapsesRapidModificationsIntoSingleUpdate) {
    const auto dir = fresh_dir("loomsearch_continuous_debounce");
    const auto db_path = dir / "index.db";
    const auto file_path = dir / "rapid.txt";

    loom::ContinuousIndexer indexer;
    loom::ContinuousIndexerOptions options;
    options.root = dir;
    options.database = db_path;
    options.debounce_delay = 100ms;

    ASSERT_TRUE(indexer.start(options));

    // 4. 短时间内连续修改 4 次
    for (int i = 0; i < 4; ++i) {
        std::ofstream file(file_path, std::ios::trunc);
        file << "rapid iteration number " << i;
        std::this_thread::sleep_for(20ms);
    }

    bool finished = poll_until([&]() {
        loom::SqliteStore store;
        store.open(db_path);
        return !store.search("number").empty();
    });

    EXPECT_TRUE(finished);

    // 最终搜索结果包含最新的 "3"
    {
        loom::SqliteStore store;
        store.open(db_path);
        EXPECT_FALSE(store.search("3").empty());
    }

    indexer.stop();
}
