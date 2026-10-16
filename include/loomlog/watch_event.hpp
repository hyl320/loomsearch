#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace loom {

enum class WatchEventType {
    Created,
    Modified,
    Deleted,
};

struct WatchEvent {
    std::filesystem::path path;
    WatchEventType type{WatchEventType::Modified};
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};

    bool operator==(const WatchEvent& other) const {
        return path == other.path && type == other.type;
    }
};

}  // namespace loom
