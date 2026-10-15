#pragma once

#include "loomlog/watch_event.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace loom {

using WatchCallback = std::function<void(const WatchEvent&)>;

// 跨平台目录监听器封装。
// Windows: ReadDirectoryChangesW + Overlapped I/O
// Linux: inotify
// macOS: FSEvents
// 后台运行专用监听工作线程，文件变动时通过回调函数通知外部。
class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // 启动监听指定根目录（递归监控所有子目录）。
    // 若目录不存在或权限不足，返回 false 并填充 error。
    bool start(const std::filesystem::path& root_dir, WatchCallback callback, std::string* error = nullptr);

    // 停止监听并等待后台线程安全退出。
    void stop();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::filesystem::path root_directory() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace loom
