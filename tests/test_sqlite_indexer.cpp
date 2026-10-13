#include "loomlog/sqlite_indexer.hpp"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "loomlog_sqlite_incremental";
    std::filesystem::path database = std::filesystem::temp_directory_path() / "loomlog_sqlite_incremental.db";

    Fixture() {
        std::filesystem::remove_all(root);
        std::filesystem::remove(database);
        std::filesystem::remove(database.string() + "-wal");
        std::filesystem::remove(database.string() + "-shm");
        std::filesystem::create_directories(root);
    }
    ~Fixture() { std::filesystem::remove_all(root); }
};

}  // namespace

TEST(SqliteIndexerTest, SkipsUnchangedFilesAndProcessesOnlyNewOrModifiedFiles) {
    Fixture fixture;
    const auto first = fixture.root / "a.txt";
    std::ofstream(first) << "alpha";

    loom::SqliteIndexer indexer;
    const loom::SqliteIndexOptions options{fixture.root, fixture.database};
    const auto initial = indexer.build(options);
    EXPECT_EQ(initial.files_total, 1u);
    EXPECT_EQ(initial.files_read, 1u);
    EXPECT_EQ(initial.files_updated, 1u);

    const auto unchanged = indexer.build(options);
    EXPECT_EQ(unchanged.files_read, 0u);
    EXPECT_EQ(unchanged.files_unchanged, 1u);

    const auto second = fixture.root / "b.txt";
    std::ofstream(second) << "beta";
    std::ofstream(first) << "alpha changed";
    const auto incremental = indexer.build(options);
    EXPECT_EQ(incremental.files_total, 2u);
    EXPECT_EQ(incremental.files_read, 2u);
    EXPECT_EQ(incremental.files_updated, 2u);
    EXPECT_EQ(incremental.files_unchanged, 0u);
}

TEST(SqliteIndexerTest, SameContentWithChangedMetadataDoesNotRewriteFtsDocument) {
    Fixture fixture;
    const auto file = fixture.root / "a.txt";
    std::ofstream(file) << "alpha";

    loom::SqliteIndexer indexer;
    const loom::SqliteIndexOptions options{fixture.root, fixture.database};
    indexer.build(options);
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now()
        + std::chrono::seconds(2));

    const auto result = indexer.build(options);
    EXPECT_EQ(result.files_read, 1u);
    EXPECT_EQ(result.files_updated, 0u);
}

TEST(SqliteIndexerTest, RemovesFilesThatDisappearedFromTheDirectory) {
    Fixture fixture;
    const auto file = fixture.root / "a.txt";
    std::ofstream(file) << "alpha";
    loom::SqliteIndexer indexer;
    const loom::SqliteIndexOptions options{fixture.root, fixture.database};
    indexer.build(options);

    std::filesystem::remove(file);
    const auto result = indexer.build(options);
    EXPECT_EQ(result.files_removed, 1u);
    loom::SqliteStore store;
    store.open(fixture.database);
    EXPECT_TRUE(store.search("alpha").empty());
    EXPECT_TRUE(store.file_states().empty());
}

TEST(SqliteIndexerTest, RejectsMissingRootWithoutChangingExistingIndex) {
    Fixture fixture;
    const auto file = fixture.root / "a.txt";
    std::ofstream(file) << "alpha";
    loom::SqliteIndexer indexer;
    const loom::SqliteIndexOptions options{fixture.root, fixture.database};
    indexer.build(options);

    std::filesystem::remove_all(fixture.root);
    EXPECT_THROW(indexer.build(options), std::invalid_argument);

    loom::SqliteStore store;
    store.open(fixture.database);
    ASSERT_EQ(store.search("alpha").size(), 1u);
}

TEST(SqliteIndexerTest, RejectsDifferentRootWithoutChangingExistingIndex) {
    Fixture fixture;
    std::ofstream(fixture.root / "a.txt") << "alpha";
    loom::SqliteIndexer indexer;
    indexer.build({fixture.root, fixture.database});

    const auto other_root = fixture.root.parent_path() / "loomlog_sqlite_incremental_other";
    std::filesystem::remove_all(other_root);
    std::filesystem::create_directories(other_root);
    std::ofstream(other_root / "b.txt") << "beta";
    EXPECT_THROW(indexer.build({other_root, fixture.database}), std::invalid_argument);

    loom::SqliteStore store;
    store.open(fixture.database);
    EXPECT_EQ(store.search("alpha").size(), 1u);
    EXPECT_TRUE(store.search("beta").empty());
    std::filesystem::remove_all(other_root);
}
