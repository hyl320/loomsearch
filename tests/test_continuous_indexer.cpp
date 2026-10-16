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

    // 确保后台 watcher 线程已进入就绪监听状态
    std::this_thread::sleep_for(100ms);

    // 1. 在目录下创建新文件
    const auto file_path = dir / "first.txt";
    {
        std::ofstream file(file_path);
        file << "apple banana cherry";
    }

    // 等待后台自动处理（通过线程安全的 stats 轮询，消除主线程并发 open 导致 SQLite WAL 竞争）
    bool indexed = poll_until([&]() {
        return indexer.stats().files_indexed >= 1;
    });
    EXPECT_TRUE(indexed);

    indexer.stop();

    // 停止后台写入后，主线程单连接打开数据库校验搜索结果
    loom::SqliteStore store;
    store.open(db_path);
    EXPECT_FALSE(store.search("banana").empty());
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

    // 记录初始基线索引计数
    const auto initial_indexed = indexer.stats().files_indexed;

    // 2. 修改文件内容，替换关键词
    std::this_thread::sleep_for(100ms);
    {
        std::ofstream file(file_path, std::ios::trunc);
        file << "revised final publication";
    }

    bool updated = poll_until([&]() {
        return indexer.stats().files_indexed > initial_indexed;
    });
    EXPECT_TRUE(updated);

    indexer.stop();

    // 验证旧词被清空，新词已入库
    loom::SqliteStore store;
    store.open(db_path);
    EXPECT_TRUE(store.search("draft").empty());
    EXPECT_FALSE(store.search("publication").empty());
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

    // 3. 删除文件
    std::this_thread::sleep_for(100ms);
    std::filesystem::remove(file_path);

    bool removed = poll_until([&]() {
        return indexer.stats().files_removed >= 1;
    });
    EXPECT_TRUE(removed);

    indexer.stop();

    loom::SqliteStore store;
    store.open(db_path);
    EXPECT_EQ(store.document_count(), 0u);
    EXPECT_TRUE(store.search("doomed").empty());
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
    const auto initial_indexed = indexer.stats().files_indexed;

    // 4. 短时间内连续修改 4 次
    for (int i = 0; i < 4; ++i) {
        std::ofstream file(file_path, std::ios::trunc);
        file << "rapid iteration number " << i;
        std::this_thread::sleep_for(20ms);
    }

    bool finished = poll_until([&]() {
        return indexer.stats().files_indexed > initial_indexed;
    });
    EXPECT_TRUE(finished);

    indexer.stop();

    // 最终搜索结果包含最新的 "3" 与 "number"
    loom::SqliteStore store;
    store.open(db_path);
    EXPECT_FALSE(store.search("number").empty());
    EXPECT_FALSE(store.search("3").empty());
}
