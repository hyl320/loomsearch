#include "loomlog/file_watcher.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <poll.h>
    #include <sys/inotify.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <CoreServices/CoreServices.h>
#endif

namespace loom {

struct FileWatcher::Impl {
    std::filesystem::path root_dir;
    WatchCallback callback;
    std::atomic<bool> is_running{false};
    std::thread worker_thread;

#if defined(_WIN32)
    HANDLE dir_handle{INVALID_HANDLE_VALUE};
    HANDLE stop_event{nullptr};
    HANDLE io_event{nullptr};

    void cleanup_handles() {
        if (dir_handle != INVALID_HANDLE_VALUE && dir_handle != nullptr) {
            ::CloseHandle(dir_handle);
            dir_handle = INVALID_HANDLE_VALUE;
        }
        if (stop_event != nullptr) {
            ::CloseHandle(stop_event);
            stop_event = nullptr;
        }
        if (io_event != nullptr) {
            ::CloseHandle(io_event);
            io_event = nullptr;
        }
    }

    void run_windows_loop() {
        std::vector<std::byte> buffer(64 * 1024);
        OVERLAPPED overlapped{};
        overlapped.hEvent = io_event;

        while (is_running.load()) {
            ::ResetEvent(io_event);
            DWORD bytes_returned = 0;
            BOOL success = ::ReadDirectoryChangesW(
                dir_handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytes_returned,
                &overlapped,
                nullptr
            );

            if (!success && ::GetLastError() != ERROR_IO_PENDING) {
                break;
            }

            HANDLE handles[2] = { stop_event, io_event };
            DWORD wait_res = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (wait_res == WAIT_OBJECT_0) {
                ::CancelIoEx(dir_handle, &overlapped);
                break;
            } else if (wait_res == WAIT_OBJECT_0 + 1) {
                DWORD transferred = 0;
                if (::GetOverlappedResult(dir_handle, &overlapped, &transferred, FALSE) && transferred > 0) {
                    std::size_t offset = 0;
                    while (offset < transferred) {
                        auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                        std::wstring wname(info->FileName, info->FileNameLength / sizeof(WCHAR));
                        auto full_path = root_dir / std::filesystem::path(wname);

                        WatchEventType event_type;
                        switch (info->Action) {
                            case FILE_ACTION_ADDED:
                                event_type = WatchEventType::Created;
                                break;
                            case FILE_ACTION_REMOVED:
                                event_type = WatchEventType::Deleted;
                                break;
                            case FILE_ACTION_MODIFIED:
                                event_type = WatchEventType::Modified;
                                break;
                            case FILE_ACTION_RENAMED_OLD_NAME:
                                event_type = WatchEventType::Deleted;
                                break;
                            case FILE_ACTION_RENAMED_NEW_NAME:
                                event_type = WatchEventType::Created;
                                break;
                            default:
                                event_type = WatchEventType::Modified;
                                break;
                        }

                        if (callback) {
                            callback(WatchEvent{full_path, event_type, std::chrono::steady_clock::now()});
                        }

                        if (info->NextEntryOffset == 0) break;
                        offset += info->NextEntryOffset;
                    }
                }
            } else {
                break;
            }
        }
    }
#elif defined(__linux__)
    int inotify_fd{-1};
    int stop_pipe[2]{-1, -1};

    void run_linux_loop() {
        // Linux inotify 循环实现（poll 监听 inotify_fd 与 stop_pipe）
        struct pollfd pfd[2];
        pfd[0].fd = stop_pipe[0];
        pfd[0].events = POLLIN;
        pfd[1].fd = inotify_fd;
        pfd[1].events = POLLIN;

        std::vector<char> buffer(4096);
        while (is_running.load()) {
            int ret = ::poll(pfd, 2, -1);
            if (ret <= 0) continue;
            if (pfd[0].revents & POLLIN) break; // 停止信号
            if (pfd[1].revents & POLLIN) {
                ssize_t len = ::read(inotify_fd, buffer.data(), buffer.size());
                if (len <= 0) break;
                ssize_t i = 0;
                while (i < len) {
                    auto* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);
                    if (event->len > 0) {
                        auto full_path = root_dir / event->name;
                        WatchEventType event_type = WatchEventType::Modified;
                        if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                            event_type = WatchEventType::Created;
                        } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            event_type = WatchEventType::Deleted;
                        } else if (event->mask & IN_MODIFY) {
                            event_type = WatchEventType::Modified;
                        }
                        if (callback) {
                            callback(WatchEvent{full_path, event_type, std::chrono::steady_clock::now()});
                        }
                    }
                    i += sizeof(struct inotify_event) + event->len;
                }
            }
        }
    }
#endif
};

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {}

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::start(const std::filesystem::path& root_dir, WatchCallback callback, std::string* error) {
    stop();

    std::error_code ec;
    if (!std::filesystem::is_directory(root_dir, ec) || ec) {
        if (error) *error = "watch root is not an accessible directory: " + root_dir.string();
        return false;
    }

    impl_->root_dir = std::filesystem::canonical(root_dir, ec);
    if (ec) {
        impl_->root_dir = root_dir.lexically_normal();
    }
    impl_->callback = std::move(callback);

#if defined(_WIN32)
    impl_->dir_handle = ::CreateFileW(
        impl_->root_dir.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (impl_->dir_handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot open directory for watching: " + root_dir.string();
        impl_->cleanup_handles();
        return false;
    }

    impl_->stop_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->io_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!impl_->stop_event || !impl_->io_event) {
        if (error) *error = "cannot create win32 sync events for watcher";
        impl_->cleanup_handles();
        return false;
    }

    impl_->is_running.store(true);
    impl_->worker_thread = std::thread([this]() {
        impl_->run_windows_loop();
    });
    return true;

#elif defined(__linux__)
    impl_->inotify_fd = ::inotify_init1(0);
    if (impl_->inotify_fd < 0) {
        if (error) *error = "inotify_init1 failed";
        return false;
    }
    if (::pipe(impl_->stop_pipe) < 0) {
        if (error) *error = "pipe creation failed";
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
        return false;
    }
    int wd = ::inotify_add_watch(impl_->inotify_fd, impl_->root_dir.c_str(),
                                 IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        if (error) *error = "inotify_add_watch failed";
        ::close(impl_->stop_pipe[0]);
        ::close(impl_->stop_pipe[1]);
        ::close(impl_->inotify_fd);
        return false;
    }

    impl_->is_running.store(true);
    impl_->worker_thread = std::thread([this]() {
        impl_->run_linux_loop();
    });
    return true;

#else
    // macOS / 其他平台通用占位
    if (error) *error = "file watcher not yet supported on this platform";
    return false;
#endif
}

void FileWatcher::stop() {
    if (!impl_ || !impl_->is_running.load()) return;

    impl_->is_running.store(false);

#if defined(_WIN32)
    if (impl_->stop_event) {
        ::SetEvent(impl_->stop_event);
    }
#elif defined(__linux__)
    if (impl_->stop_pipe[1] != -1) {
        char c = 'q';
        (void)::write(impl_->stop_pipe[1], &c, 1);
    }
#endif

    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }

#if defined(_WIN32)
    impl_->cleanup_handles();
#elif defined(__linux__)
    if (impl_->inotify_fd != -1) {
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }
    if (impl_->stop_pipe[0] != -1) {
        ::close(impl_->stop_pipe[0]);
        ::close(impl_->stop_pipe[1]);
        impl_->stop_pipe[0] = -1;
        impl_->stop_pipe[1] = -1;
    }
#endif
}

bool FileWatcher::is_running() const noexcept {
    return impl_ && impl_->is_running.load();
}

std::filesystem::path FileWatcher::root_directory() const {
    return impl_ ? impl_->root_dir : std::filesystem::path{};
}

}  // namespace loom
