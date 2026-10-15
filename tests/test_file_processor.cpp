#include "loomlog/file_processor.hpp"

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

TEST(FileProcessorTest, ReadsTextAndExtractsLowercaseWords) {
    const auto dir = fresh_dir("loomsearch_file_processor_words");
    const auto path = dir / "sample.md";
    std::ofstream(path) << "Thread pool\nLOGGER thread";

    const auto result = loom::FileProcessor{}.process(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.path, path.lexically_normal().string());
    EXPECT_TRUE(result.words.contains("thread"));
    EXPECT_TRUE(result.words.contains("pool"));
    EXPECT_TRUE(result.words.contains("logger"));
    EXPECT_EQ(result.words.at("thread"), 2u);
    EXPECT_EQ(result.words.at("pool"), 1u);
    EXPECT_EQ(result.words.at("logger"), 1u);
}

TEST(FileProcessorTest, CountsBytesFromFileContent) {
    const auto dir = fresh_dir("loomsearch_file_processor_bytes");
    const auto path = dir / "sample.txt";
    std::ofstream(path, std::ios::binary) << "abc\n";

    const auto result = loom::FileProcessor{}.process(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.bytes, 4u);
}

TEST(FileProcessorTest, MissingFileReturnsFailureResult) {
    const auto dir = fresh_dir("loomsearch_file_processor_missing");

    const auto result = loom::FileProcessor{}.process(dir / "missing.txt");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}
