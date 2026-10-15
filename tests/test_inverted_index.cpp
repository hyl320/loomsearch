#include "loomlog/inverted_index.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(InvertedIndexTest, AddsDocumentsForEachWord) {
    loom::InvertedIndex index;

    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"pool", "logger"});

    EXPECT_EQ(index.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(index.find("logger"), std::vector<std::string>{"b.md"});
    EXPECT_EQ(index.find("pool"), (std::vector<std::string>{"a.md", "b.md"}));
}

TEST(InvertedIndexTest, DoesNotDuplicateSameDocumentForSameWord) {
    loom::InvertedIndex index;

    index.add_document("a.md", {"thread"});
    index.add_document("a.md", {"thread"});

    EXPECT_EQ(index.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(index.document_count(), 1u);
}

TEST(InvertedIndexTest, SnapshotIsSortedByKeywordAndPath) {
    loom::InvertedIndex index;

    index.add_document("c.cpp", {"pool"});
    index.add_document("a.md", {"thread", "pool"});

    const auto snapshot = index.snapshot();

    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].first, "pool");
    EXPECT_EQ(snapshot[0].second, (std::vector<std::string>{"a.md", "c.cpp"}));
    EXPECT_EQ(snapshot[1].first, "thread");
}

TEST(InvertedIndexTest, StoresAndQueriesTermFrequency) {
    loom::InvertedIndex index;
    const std::unordered_map<std::string, std::size_t> words_a{
        {"thread", 3},
        {"pool", 1},
    };
    const std::unordered_map<std::string, std::size_t> words_b{
        {"pool", 2},
        {"logger", 5},
    };

    index.add_document("a.md", words_a);
    index.add_document("b.md", words_b);

    EXPECT_EQ(index.term_frequency("thread", "a.md"), 3u);
    EXPECT_EQ(index.term_frequency("pool", "a.md"), 1u);
    EXPECT_EQ(index.term_frequency("pool", "b.md"), 2u);
    EXPECT_EQ(index.term_frequency("logger", "b.md"), 5u);
    EXPECT_EQ(index.term_frequency("thread", "b.md"), 0u);
    EXPECT_EQ(index.term_frequency("nonexistent", "a.md"), 0u);
    EXPECT_EQ(index.term_frequency("thread", "nonexistent.md"), 0u);
}

TEST(InvertedIndexTest, AccumulatesTermFrequencyOnRepeatedAddDocument) {
    loom::InvertedIndex index;
    index.add_document("a.md", std::unordered_map<std::string, std::size_t>{{"thread", 2}});
    index.add_document("a.md", std::unordered_map<std::string, std::size_t>{{"thread", 3}});

    EXPECT_EQ(index.term_frequency("thread", "a.md"), 5u);
    EXPECT_EQ(index.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(index.document_count(), 1u);
}
