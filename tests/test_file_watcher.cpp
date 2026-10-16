#include "loomlog/file_watcher.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <mutex>
#include <vector>

namespace {

std::filesystem::path fresh_dir(std::string_view name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST(FileWatcherTest, RejectsNonExistentDirectory) {
    loom::FileWatcher watcher;
    std::string error;
    bool ok = watcher.start("non_existent_directory_xyz_123", [](const loom::WatchEvent&) {}, &error);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(watcher.is_running());
}

TEST(FileWatcherTest, StartsAndStopsCleanly) {
    const auto dir = fresh_dir("loomsearch_watcher_start_stop");
    loom::FileWatcher watcher;

    ASSERT_TRUE(watcher.start(dir, [](const loom::WatchEvent&) {}));
    EXPECT_TRUE(watcher.is_running());
    EXPECT_EQ(watcher.root_directory(), std::filesystem::canonical(dir));

    watcher.stop();
    EXPECT_FALSE(watcher.is_running());
}

TEST(FileWatcherTest, DetectsFileCreationModificationAndDeletion) {
    const auto dir = fresh_dir("loomsearch_watcher_crud");
    const auto file_path = dir / "hello.txt";

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<loom::WatchEvent> events;

    loom::FileWatcher watcher;
    bool started = watcher.start(dir, [&](const loom::WatchEvent& ev) {
        std::lock_guard<std::mutex> lock(mtx);
        events.push_back(ev);
        cv.notify_all();
    });
    ASSERT_TRUE(started);

    auto wait_for_event = [&](loom::WatchEventType expected_type, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) -> bool {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, timeout, [&]() {
            for (const auto& ev : events) {
                if (ev.path.filename() == "hello.txt" && ev.type == expected_type) {
                    return true;
                }
            }
            return false;
        });
    };

    // 1. 创建文件
    {
        std::ofstream file(file_path);
        file << "first version";
    }
    EXPECT_TRUE(wait_for_event(loom::WatchEventType::Created) || wait_for_event(loom::WatchEventType::Modified));

    // 2. 修改文件
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ofstream file(file_path, std::ios::app);
        file << "\nsecond version";
    }
    EXPECT_TRUE(wait_for_event(loom::WatchEventType::Modified));

    // 3. 删除文件
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::filesystem::remove(file_path);
    }
    EXPECT_TRUE(wait_for_event(loom::WatchEventType::Deleted));

    watcher.stop();
    EXPECT_FALSE(watcher.is_running());
}
