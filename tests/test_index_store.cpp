#include "loomlog/index_store.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

TEST(IndexStoreTest, SavesAndLoadsIndex) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_roundtrip";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";

    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"pool"});

    loom::IndexStore{}.save(index, path);
    const auto loaded = loom::IndexStore{}.load(path);

    EXPECT_EQ(loaded.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(loaded.find("pool"), (std::vector<std::string>{"a.md", "b.md"}));
}

TEST(IndexStoreTest, RejectsBadHeader) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_bad_header";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";
    std::ofstream(path) << "BAD\n";

    EXPECT_THROW(loom::IndexStore{}.load(path), std::runtime_error);
}

TEST(IndexStoreTest, RejectsMissingIndexFile) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_missing";
    std::filesystem::remove_all(dir);

    EXPECT_THROW(loom::IndexStore{}.load(dir / "missing.bin"), std::runtime_error);
}

TEST(IndexStoreTest, AtomicSaveLeavesNoTemporaryFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_atomic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";

    loom::InvertedIndex index;
    index.add_document("doc.txt", {"atomic", "persistence"});

    loom::IndexStore{}.save(index, path);
    EXPECT_TRUE(std::filesystem::exists(path));

    // 检查目录内只有目标文件，没有任何残留的 .tmp 文件
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        EXPECT_EQ(entry.path().filename().string(), "index.bin");
        ++count;
    }
    EXPECT_EQ(count, 1);
}

TEST(IndexStoreTest, RejectsIncompatibleVersionHeader) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_v99";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";

    std::ofstream(path) << "LOOMSEARCH_INDEX_V99\n0\n";
    EXPECT_THROW(loom::IndexStore{}.load(path), std::runtime_error);
}
