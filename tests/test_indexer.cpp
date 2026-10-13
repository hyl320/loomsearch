#include "loomlog/indexer.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

TEST(IndexerTest, BuildsIndexFromSupportedFilesInParallel) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_parallel";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "a.md") << "thread pool";
    std::ofstream(root / "b.txt") << "thread logger";
    std::ofstream(root / "skip.png") << "thread pool";

    loom::IndexBuildOptions options;
    options.root = root;
    options.thread_count = 2;

    const auto result = loom::Indexer{}.build(options);

    EXPECT_EQ(result.stats.files_total, 2u);
    EXPECT_EQ(result.stats.files_success, 2u);
    EXPECT_EQ(result.stats.files_failed, 0u);
    EXPECT_EQ(result.stats.bytes_total, 24u);
    EXPECT_EQ(result.index.find("thread").size(), 2u);
    EXPECT_EQ(
        result.index.find("pool"),
        std::vector<std::string>{(root / "a.md").lexically_normal().string()}
    );
}

TEST(IndexerTest, EmptyDirectoryProducesEmptyIndex) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_empty";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    loom::IndexBuildOptions options;
    options.root = root;
    options.thread_count = 2;

    const auto result = loom::Indexer{}.build(options);

    EXPECT_EQ(result.stats.files_total, 0u);
    EXPECT_EQ(result.stats.files_success, 0u);
    EXPECT_EQ(result.stats.files_failed, 0u);
    EXPECT_EQ(result.index.keyword_count(), 0u);
}

TEST(IndexerTest, ExplicitFilesCountFailedProcessingResults) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_failures";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto existing = root / "a.txt";
    const auto missing = root / "missing.txt";
    std::ofstream(existing) << "thread pool";

    const auto result = loom::Indexer{}.build_files({existing, missing}, 2);

    EXPECT_EQ(result.stats.files_total, 2u);
    EXPECT_EQ(result.stats.files_success, 1u);
    EXPECT_EQ(result.stats.files_failed, 1u);
    EXPECT_EQ(result.index.find("thread"), std::vector<std::string>{existing.lexically_normal().string()});
}

TEST(IndexerTest, ProcessesFilesBeyondInFlightWindowIncludingFailures) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_window";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::vector<std::filesystem::path> files;
    for (int i = 0; i < 70; ++i) {
        const auto path = root / ("window_" + std::to_string(i) + ".txt");
        if (i != 31 && i != 32) {
            std::ofstream(path) << "thread pool";
        }
        files.push_back(path);
    }

    const auto result = loom::Indexer{}.build_files(files, 4);
    EXPECT_EQ(result.stats.files_total, 70u);
    EXPECT_EQ(result.stats.files_success, 68u);
    EXPECT_EQ(result.stats.files_failed, 2u);
    EXPECT_EQ(result.stats.bytes_total, 68u * 11u);
    EXPECT_EQ(result.index.document_count(), 68u);
    EXPECT_EQ(result.index.find("thread").size(), 68u);

    for (int i = 0; i < 70; ++i) {
        std::filesystem::remove(files[i]);
    }
    std::filesystem::remove(root);
}
