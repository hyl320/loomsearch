#include "loomlog/text_analyzer.hpp"

#include <gtest/gtest.h>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(TextAnalyzerTest, TokenizesAsciiWordsAsLowercaseUniqueTerms) {
    const auto words = loom::tokenize_words("Thread_pool THREAD logger.md 42");
    const std::unordered_set<std::string> expected{
        "thread",
        "pool",
        "logger",
        "md",
        "42",
    };

    EXPECT_EQ(words, expected);
}

TEST(TextAnalyzerTest, NormalizesQueryTermsInInputOrderWithoutDuplicates) {
    const auto terms = loom::normalize_query_terms("Thread pool THREAD");
    const std::vector<std::string> expected{"thread", "pool"};

    EXPECT_EQ(terms, expected);
}

TEST(TextAnalyzerTest, TokenizeWithFrequencyCountsWordOccurrences) {
    const auto freq = loom::tokenize_with_frequency("thread pool thread THREAD pool logger");
    const std::unordered_map<std::string, std::size_t> expected{
        {"thread", 3},
        {"pool", 2},
        {"logger", 1},
    };

    EXPECT_EQ(freq, expected);
}

TEST(TextAnalyzerTest, TokenizeWithFrequencyHandlesEmptyInput) {
    const auto freq = loom::tokenize_with_frequency("");
    EXPECT_TRUE(freq.empty());
}

TEST(TextAnalyzerTest, TokenizeWithFrequencySingleWord) {
    const auto freq = loom::tokenize_with_frequency("hello");
    ASSERT_EQ(freq.size(), 1u);
    EXPECT_EQ(freq.at("hello"), 1u);
}

