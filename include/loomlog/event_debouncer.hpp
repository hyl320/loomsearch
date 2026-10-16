#pragma once

#include "loomlog/watch_event.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loom {

// 文件变更事件防抖与状态机合并器。
//
// 核心职责：
// 1. 时间窗口防抖：同一文件高频事件在窗口期内被压制，直到冷却结束（quiet period）才下发；
// 2. 状态机合并：
//    - Created + Modified -> Created
//    - Created + Deleted  -> 抵消（从缓冲中丢弃）
//    - Modified + Modified -> Modified
//    - Modified + Deleted -> Deleted
//    - Deleted + Created  -> Modified
// 3. 线程安全：支持多源并发 post 与外部定期 drain_ready。
class EventDebouncer {
public:
    explicit EventDebouncer(std::chrono::milliseconds debounce_delay = std::chrono::milliseconds(100));

    // 投递原始事件
    void post(WatchEvent event);
    void post(const std::filesystem::path& path, WatchEventType type);

    // 刷出所有已经稳定（距离最后一次更新已超过 debounce_delay）的事件
    std::vector<WatchEvent> drain_ready(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()
    );

    // 强制立即刷出所有缓冲中的事件（不论是否冷却完毕）
    std::vector<WatchEvent> flush_all();

    // 当前未稳定的缓冲事件数量
    [[nodiscard]] std::size_t pending_count() const;

    // 获取设定的防抖延迟
    [[nodiscard]] std::chrono::milliseconds debounce_delay() const noexcept;

    // 清空缓冲区
    void clear();

private:
    struct PendingItem {
        WatchEvent event;
        std::chrono::steady_clock::time_point last_active;
    };

    mutable std::mutex mutex_;
    std::chrono::milliseconds debounce_delay_;
    std::unordered_map<std::string, PendingItem> pending_;
};

}  // namespace loom
