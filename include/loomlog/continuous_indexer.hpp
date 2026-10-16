#pragma once

#include "loomlog/file_processor.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace loom {

struct ContinuousIndexerOptions {
    std::filesystem::path root;
    std::filesystem::path database;
    std::chrono::milliseconds debounce_delay{100};
    std::size_t max_file_size{FileProcessor::kDefaultMaxFileSize};
};

struct ContinuousStats {
    std::size_t events_received{0};
    std::size_t files_indexed{0};
    std::size_t files_removed{0};
};

// 后台持续增量更新引擎。
// 整合 FileWatcher、EventDebouncer、MemoryMappedFile、SqliteStore。
// 保证：
// 1. 文件创建 -> 自动加入索引
// 2. 文件修改 -> 自动重新索引
// 3. 文件删除 -> 自动移除
// 4. 短时间连续修改 -> 防抖合并成一次处理
class ContinuousIndexer {
public:
    ContinuousIndexer();
    ~ContinuousIndexer();

    ContinuousIndexer(const ContinuousIndexer&) = delete;
    ContinuousIndexer& operator=(const ContinuousIndexer&) = delete;

    bool start(const ContinuousIndexerOptions& options, std::string* error = nullptr);
    void stop();

    // 显式处理当前已就绪的防抖事件（支持测试快速驱动）
    void process_ready_events();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] ContinuousStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace loom
