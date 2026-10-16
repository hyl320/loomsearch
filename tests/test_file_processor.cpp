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

TEST(FileProcessorTest, EmptyFileSucceedsWithZeroBytes) {
    const auto dir = fresh_dir("loomsearch_file_processor_empty");
    const auto path = dir / "empty.txt";
    std::ofstream(path, std::ios::binary);

    const auto result = loom::FileProcessor{}.process(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.bytes, 0u);
    EXPECT_TRUE(result.words.empty());
}

TEST(FileProcessorTest, RejectsFileExceedingMaxFileSizeLimit) {
    const auto dir = fresh_dir("loomsearch_file_processor_limit");
    const auto path = dir / "large.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << std::string(128, 'x');
    }

    // 设置最大限制为 64 字节，文件为 128 字节
    loom::FileProcessor processor(64);
    const auto result = processor.process(path);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("exceeds limit") != std::string::npos) << result.error;
}

