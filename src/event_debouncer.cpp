#include "loomlog/event_debouncer.hpp"

#include <algorithm>

namespace loom {

EventDebouncer::EventDebouncer(std::chrono::milliseconds debounce_delay)
    : debounce_delay_(debounce_delay) {}

void EventDebouncer::post(WatchEvent event) {
    const auto key = event.path.lexically_normal().string();
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pending_.find(key);
    if (it == pending_.end()) {
        const auto ts = event.timestamp;
        pending_.emplace(key, PendingItem{std::move(event), ts});
        return;
    }

    it->second.last_active = event.timestamp;
    const auto existing_type = it->second.event.type;

    switch (existing_type) {
        case WatchEventType::Created:
            if (event.type == WatchEventType::Deleted) {
                // 新增后立刻被删除，前后相互抵消
                pending_.erase(it);
            }
            // 若为 Modified 或重复 Created，整体操作依然是 Created
            break;

        case WatchEventType::Modified:
            if (event.type == WatchEventType::Deleted) {
                // 历史文件在修改后最终被删除
                it->second.event.type = WatchEventType::Deleted;
            }
            // 若为 Modified，保持 Modified
            break;

        case WatchEventType::Deleted:
            if (event.type == WatchEventType::Created || event.type == WatchEventType::Modified) {
                // 删了之后又重新创建或写入，等效于内容被彻底覆盖更新 (Modified)
                it->second.event.type = WatchEventType::Modified;
            }
            // 若为重复 Deleted，保持 Deleted
            break;
    }
}

void EventDebouncer::post(const std::filesystem::path& path, WatchEventType type) {
    post(WatchEvent{path, type, std::chrono::steady_clock::now()});
}

std::vector<WatchEvent> EventDebouncer::drain_ready(std::chrono::steady_clock::time_point now) {
    std::vector<WatchEvent> ready;
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.last_active + debounce_delay_ <= now) {
            ready.push_back(std::move(it->second.event));
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }

    std::sort(ready.begin(), ready.end(), [](const WatchEvent& a, const WatchEvent& b) {
        return a.path.string() < b.path.string();
    });

    return ready;
}

std::vector<WatchEvent> EventDebouncer::flush_all() {
    std::vector<WatchEvent> all;
    std::lock_guard<std::mutex> lock(mutex_);

    all.reserve(pending_.size());
    for (auto& [_, item] : pending_) {
        all.push_back(std::move(item.event));
    }
    pending_.clear();

    std::sort(all.begin(), all.end(), [](const WatchEvent& a, const WatchEvent& b) {
        return a.path.string() < b.path.string();
    });

    return all;
}

std::size_t EventDebouncer::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::chrono::milliseconds EventDebouncer::debounce_delay() const noexcept {
    return debounce_delay_;
}

void EventDebouncer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

}  // namespace loom
