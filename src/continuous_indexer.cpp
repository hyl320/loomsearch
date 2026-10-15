#include "loomlog/continuous_indexer.hpp"

#include "loomlog/directory_scanner.hpp"
#include "loomlog/event_debouncer.hpp"
#include "loomlog/file_watcher.hpp"
#include "loomlog/mmap_file.hpp"
#include "loomlog/sqlite_indexer.hpp"
#include "loomlog/sqlite_store.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace loom {
namespace {

std::string lowercase_ext(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

bool is_supported_file_type(const std::filesystem::path& path) {
    static constexpr std::array extensions{".txt", ".md", ".cpp", ".hpp"};
    const auto ext = lowercase_ext(path);
    return std::ranges::find(extensions, ext) != extensions.end();
}

std::int64_t file_write_time(const std::filesystem::path& path) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

std::string compute_content_hash(std::string_view content) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : content) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}

}  // namespace

struct ContinuousIndexer::Impl {
    ContinuousIndexerOptions options;
    std::unique_ptr<SqliteStore> store;
    std::unique_ptr<FileWatcher> watcher;
    std::unique_ptr<EventDebouncer> debouncer;

    std::atomic<bool> is_running{false};
    std::thread worker_thread;
    mutable std::mutex process_mutex;
    ContinuousStats stats;

    void process_events(const std::vector<WatchEvent>& events) {
        if (events.empty() || !store) return;

        std::lock_guard<std::mutex> lock(process_mutex);
        store->set_build_status("syncing");
        store->begin_transaction();

        try {
            for (const auto& ev : events) {
                const auto path_str = ev.path.lexically_normal().string();
                ++stats.events_received;

                if (ev.type == WatchEventType::Deleted) {
                    store->remove(path_str);
                    store->remove_file_state(path_str);
                    ++stats.files_removed;
                    continue;
                }

                // 对于 Created / Modified，过滤受支持的文件格式
                if (!is_supported_file_type(ev.path)) {
                    continue;
                }

                std::error_code ec;
                if (!std::filesystem::exists(ev.path, ec) || ec) {
                    // 若文件已不存在，等同于删除
                    store->remove(path_str);
                    store->remove_file_state(path_str);
                    ++stats.files_removed;
                    continue;
                }

                const auto size = std::filesystem::file_size(ev.path, ec);
                if (ec || size > options.max_file_size) {
                    continue;
                }

                std::string content;
                if (size > 0) {
                    MemoryMappedFile mmap;
                    if (!mmap.open(ev.path)) {
                        continue;
                    }
                    content = std::string(mmap.string_view());
                }

                const auto write_time = file_write_time(ev.path);
                const auto hash = compute_content_hash(content);

                store->update(path_str, content);
                store->upsert_file_state(SqliteFileState{
                    path_str,
                    size,
                    write_time,
                    hash
                });
                ++stats.files_indexed;
            }

            store->commit_transaction();
            store->set_build_status("ready");
        } catch (...) {
            store->rollback_transaction();
            store->set_build_status("ready");
            throw;
        }
    }
};

ContinuousIndexer::ContinuousIndexer() : impl_(std::make_unique<Impl>()) {}

ContinuousIndexer::~ContinuousIndexer() {
    stop();
}

bool ContinuousIndexer::start(const ContinuousIndexerOptions& options, std::string* error) {
    stop();

    std::error_code ec;
    if (!std::filesystem::is_directory(options.root, ec) || ec) {
        if (error) *error = "continuous index root is not a valid directory: " + options.root.string();
        return false;
    }

    impl_->options = options;
    impl_->options.root = std::filesystem::canonical(options.root, ec);
    if (ec) impl_->options.root = options.root.lexically_normal();

    // 1. 基线构建：先调用现有增量构建保证存量文件被正确索引
    try {
        SqliteIndexer{}.build({impl_->options.root, impl_->options.database});
    } catch (const std::exception& e) {
        if (error) *error = std::string("failed baseline indexing: ") + e.what();
        return false;
    }

    // 2. 打开 SQLite Store 并执行崩溃自愈检查
    impl_->store = std::make_unique<SqliteStore>();
    try {
        impl_->store->open(impl_->options.database);
        impl_->store->recover_clean_state();
    } catch (const std::exception& e) {
        if (error) *error = std::string("failed to open database: ") + e.what();
        impl_->store.reset();
        return false;
    }

    // 3. 初始化防抖与事件监听
    impl_->debouncer = std::make_unique<EventDebouncer>(options.debounce_delay);
    impl_->watcher = std::make_unique<FileWatcher>();

    bool watcher_ok = impl_->watcher->start(
        impl_->options.root,
        [this](const WatchEvent& event) {
            if (impl_->debouncer) {
                impl_->debouncer->post(event);
            }
        },
        error
    );

    if (!watcher_ok) {
        impl_->store.reset();
        impl_->debouncer.reset();
        impl_->watcher.reset();
        return false;
    }

    impl_->is_running.store(true);
    impl_->worker_thread = std::thread([this]() {
        while (impl_->is_running.load()) {
            process_ready_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    return true;
}

void ContinuousIndexer::stop() {
    if (!impl_ || !impl_->is_running.load()) return;

    impl_->is_running.store(false);

    if (impl_->watcher) {
        impl_->watcher->stop();
    }

    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }

    // 退出前刷出所有未处理的防抖事件并提交入库
    if (impl_->debouncer && impl_->store) {
        const auto remaining = impl_->debouncer->flush_all();
        impl_->process_events(remaining);
    }

    if (impl_->store) {
        impl_->store->close();
        impl_->store.reset();
    }

    impl_->watcher.reset();
    impl_->debouncer.reset();
}

void ContinuousIndexer::process_ready_events() {
    if (!impl_ || !impl_->debouncer) return;
    const auto ready = impl_->debouncer->drain_ready();
    impl_->process_events(ready);
}

bool ContinuousIndexer::is_running() const noexcept {
    return impl_ && impl_->is_running.load();
}

ContinuousStats ContinuousIndexer::stats() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->process_mutex);
    return impl_->stats;
}

}  // namespace loom
