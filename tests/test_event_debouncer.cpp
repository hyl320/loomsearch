#include "loomlog/event_debouncer.hpp"

#include <chrono>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(EventDebouncerTest, CollapsesContinuousModificationsIntoSingleModify) {
    loom::EventDebouncer debouncer(100ms);
    const auto base_time = std::chrono::steady_clock::now();
    const std::filesystem::path file = "test_dir/file.txt";

    // 模拟在 0ms, 20ms, 40ms, 60ms 连续 4 次修改
    for (int i = 0; i < 4; ++i) {
        debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Modified, base_time + std::chrono::milliseconds(i * 20)});
    }

    EXPECT_EQ(debouncer.pending_count(), 1u);

    // 在 80ms 时检查，最后一次活跃是 60ms，距离 80ms 仅过去 20ms，还未冷却
    auto ready = debouncer.drain_ready(base_time + 80ms);
    EXPECT_TRUE(ready.empty());
    EXPECT_EQ(debouncer.pending_count(), 1u);

    // 在 160ms（60ms + 100ms）时检查，冷却完毕，刷出唯一的 1 个 Modified 事件
    ready = debouncer.drain_ready(base_time + 160ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].path, file.lexically_normal());
    EXPECT_EQ(ready[0].type, loom::WatchEventType::Modified);
    EXPECT_EQ(debouncer.pending_count(), 0u);
}

TEST(EventDebouncerTest, CollapsesCreateThenModifyIntoSingleCreate) {
    loom::EventDebouncer debouncer(50ms);
    const auto t0 = std::chrono::steady_clock::now();
    const std::filesystem::path file = "doc.md";

    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Created, t0});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Modified, t0 + 10ms});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Modified, t0 + 20ms});

    auto ready = debouncer.drain_ready(t0 + 80ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].type, loom::WatchEventType::Created);
}

TEST(EventDebouncerTest, CancelsCreateFollowedByDelete) {
    loom::EventDebouncer debouncer(50ms);
    const auto t0 = std::chrono::steady_clock::now();
    const std::filesystem::path file = "temp.tmp";

    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Created, t0});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Modified, t0 + 5ms});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Deleted, t0 + 10ms});

    EXPECT_EQ(debouncer.pending_count(), 0u);
    auto ready = debouncer.drain_ready(t0 + 100ms);
    EXPECT_TRUE(ready.empty());
}

TEST(EventDebouncerTest, CollapsesModifyFollowedByDeleteIntoSingleDelete) {
    loom::EventDebouncer debouncer(50ms);
    const auto t0 = std::chrono::steady_clock::now();
    const std::filesystem::path file = "existing.txt";

    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Modified, t0});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Deleted, t0 + 10ms});

    auto ready = debouncer.drain_ready(t0 + 70ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].type, loom::WatchEventType::Deleted);
}

TEST(EventDebouncerTest, CollapsesDeleteFollowedByCreateIntoModify) {
    loom::EventDebouncer debouncer(50ms);
    const auto t0 = std::chrono::steady_clock::now();
    const std::filesystem::path file = "recreated.cpp";

    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Deleted, t0});
    debouncer.post(loom::WatchEvent{file, loom::WatchEventType::Created, t0 + 10ms});

    auto ready = debouncer.drain_ready(t0 + 70ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].type, loom::WatchEventType::Modified);
}

TEST(EventDebouncerTest, FlushAllImmediatelyReturnsAllPending) {
    loom::EventDebouncer debouncer(1000ms);
    debouncer.post("a.txt", loom::WatchEventType::Created);
    debouncer.post("b.txt", loom::WatchEventType::Modified);

    auto all = debouncer.flush_all();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].path.filename().string(), "a.txt");
    EXPECT_EQ(all[1].path.filename().string(), "b.txt");
    EXPECT_EQ(debouncer.pending_count(), 0u);
}

TEST(EventDebouncerTest, IndependentPathsDoNotInterfere) {
    loom::EventDebouncer debouncer(50ms);
    const auto t0 = std::chrono::steady_clock::now();

    debouncer.post(loom::WatchEvent{"first.txt", loom::WatchEventType::Modified, t0});
    debouncer.post(loom::WatchEvent{"second.txt", loom::WatchEventType::Created, t0 + 100ms});

    // 在 t0 + 60ms 时，first 已冷却，second 刚加入还没冷却
    auto ready = debouncer.drain_ready(t0 + 60ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].path.filename().string(), "first.txt");
    EXPECT_EQ(debouncer.pending_count(), 1u);

    // 在 t0 + 160ms 时，second 也冷却
    ready = debouncer.drain_ready(t0 + 160ms);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].path.filename().string(), "second.txt");
    EXPECT_EQ(debouncer.pending_count(), 0u);
}
