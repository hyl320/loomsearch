#include "loomlog/mmap_file.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string_view>

namespace {

std::filesystem::path fresh_dir(std::string_view name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST(MmapFileTest, MapsNormalFileAndReadsContent) {
    const auto dir = fresh_dir("loomsearch_mmap_normal");
    const auto path = dir / "hello.txt";
    const std::string content = "Hello Memory Mapped File!\nLine 2";
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }

    loom::MemoryMappedFile mmap;
    std::string error;
    ASSERT_TRUE(mmap.open(path, &error)) << error;
    EXPECT_TRUE(mmap.is_open());
    EXPECT_EQ(mmap.size(), content.size());
    EXPECT_EQ(mmap.string_view(), content);

    mmap.close();
    EXPECT_FALSE(mmap.is_open());
    EXPECT_EQ(mmap.size(), 0u);
    EXPECT_TRUE(mmap.string_view().empty());
}

TEST(MmapFileTest, HandlesEmptyFileSafely) {
    const auto dir = fresh_dir("loomsearch_mmap_empty");
    const auto path = dir / "empty.txt";
    {
        std::ofstream file(path, std::ios::binary);
    }

    loom::MemoryMappedFile mmap;
    std::string error;
    ASSERT_TRUE(mmap.open(path, &error)) << error;
    EXPECT_TRUE(mmap.is_open());
    EXPECT_EQ(mmap.size(), 0u);
    EXPECT_TRUE(mmap.string_view().empty());
}

TEST(MmapFileTest, ReturnsFalseForNonExistentFile) {
    const auto dir = fresh_dir("loomsearch_mmap_missing");
    const auto path = dir / "not_exist.txt";

    loom::MemoryMappedFile mmap;
    std::string error;
    EXPECT_FALSE(mmap.open(path, &error));
    EXPECT_FALSE(mmap.is_open());
    EXPECT_FALSE(error.empty());
}

TEST(MmapFileTest, SupportsMoveSemantics) {
    const auto dir = fresh_dir("loomsearch_mmap_move");
    const auto path = dir / "move_test.txt";
    const std::string content = "Move semantics test";
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }

    loom::MemoryMappedFile original;
    ASSERT_TRUE(original.open(path));

    loom::MemoryMappedFile moved(std::move(original));
    EXPECT_FALSE(original.is_open());
    EXPECT_TRUE(moved.is_open());
    EXPECT_EQ(moved.string_view(), content);

    loom::MemoryMappedFile assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(moved.is_open());
    EXPECT_TRUE(assigned.is_open());
    EXPECT_EQ(assigned.string_view(), content);
}
