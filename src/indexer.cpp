#include "loomlog/indexer.hpp"

#include "loomlog/directory_scanner.hpp"
#include "loomlog/file_processor.hpp"
#include "loomlog/file_result.hpp"
#include "loomlog/thread_pool.hpp"

#include <chrono>
#include <deque>
#include <exception>
#include <future>
#include <string>
#include <utility>

namespace loom {

IndexBuildResult Indexer::build(const IndexBuildOptions& options) const {
    // 第一步，扫描目录 得到的 files 是一个文件路径列表
    const auto files = DirectoryScanner{}.scan(options.root);
    // 记录日志
    if (options.logger != nullptr) {
        options.logger->info(
            "index started root={} files={}",
            options.root.lexically_normal().string(),
            files.size()
        );
    }
    //第三步，正式构建索引：
    return build_files(files, options.thread_count, options.logger);
}

IndexBuildResult Indexer::build_files(
    std::vector<std::filesystem::path> files,
    std::size_t thread_count,
    Logger* logger
) const {
    //先记录开始时间
    const auto started = std::chrono::steady_clock::now();

    IndexBuildResult result;
    //表示这次要处理的文件总数。
    result.stats.files_total = files.size();

    ThreadPool pool(thread_count);
    constexpr std::size_t max_in_flight = 32;
    std::deque<std::future<FileResult>> futures;
    std::size_t next_file = 0;
    const auto submit_next = [&] {
        auto file = std::move(files[next_file]);
        futures.push_back(pool.submit([file = std::move(file)] {
            return FileProcessor{}.process(file);
        }));
        ++next_file;
    };

    while (next_file < files.size() && futures.size() < max_in_flight) {
        submit_next();
    }

    // 主线程按提交顺序合并；取回一个结果后才补交一个任务。
    while (!futures.empty()) {
        auto future = std::move(futures.front());
        futures.pop_front();
        try {
            auto file_result = future.get();
            if (!file_result.success) {
                ++result.stats.files_failed;
                if (logger != nullptr) {
                    logger->warn("file failed path={} error={}", file_result.path, file_result.error);
                }
            } else {
                result.stats.bytes_total += file_result.bytes;
                result.stats.keywords_total += file_result.words.size();
                ++result.stats.files_success;

                if (logger != nullptr) {
                    logger->info(
                        "file processed path={} bytes={} words={}",
                        file_result.path,
                        file_result.bytes,
                        file_result.words.size()
                    );
                }
                result.index.add_document(file_result.path, file_result.words);
            }
        } catch (const std::exception& error) {
            ++result.stats.files_failed;
            if (logger != nullptr) {
                logger->error("file task threw error={}", error.what());
            }
        }
        if (next_file < files.size()) {
            submit_next();
        }
    }

    pool.shutdown();

    result.stats.elapsed_ms = static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started
        ).count()
    );

    if (logger != nullptr) {
        logger->info(
            "index finished files_total={} files_success={} files_failed={} bytes_total={} elapsed_ms={}",
            result.stats.files_total,
            result.stats.files_success,
            result.stats.files_failed,
            result.stats.bytes_total,
            result.stats.elapsed_ms
        );
    }

    return result;
}

}  // namespace loom
