#include "loomlog/query_engine.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::vector<std::string> paths_of(const std::vector<loom::QueryResult>& results) {
    std::vector<std::string> paths;
    paths.reserve(results.size());
    for (const auto& r : results) {
        paths.push_back(r.path);
    }
    return paths;
}

}  // namespace

TEST(QueryEngineTest, SingleKeywordReturnsMatchingDocuments) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"logger"});

    const auto results = loom::QueryEngine{}.query(index, "THREAD");
    EXPECT_EQ(paths_of(results), std::vector<std::string>{"a.md"});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GT(results[0].score, 0.0);
}

TEST(QueryEngineTest, MultipleKeywordsUseAndSemantics) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"thread"});
    index.add_document("c.md", {"pool"});

    EXPECT_EQ(paths_of(loom::QueryEngine{}.query(index, "thread pool")), std::vector<std::string>{"a.md"});
}

TEST(QueryEngineTest, EmptyQueryReturnsNoMatches) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread"});

    EXPECT_TRUE(loom::QueryEngine{}.query(index, "!!!").empty());
}

TEST(QueryEngineTest, MissingTermMakesAndQueryEmpty) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});

    EXPECT_TRUE(loom::QueryEngine{}.query(index, "thread logger").empty());
}

TEST(QueryEngineTest, MultipleTermsAgreeRegardlessOfPostingListOrder) {
    loom::InvertedIndex index;
    index.add_document("z.md", {"common", "medium", "rare"});
    index.add_document("a.md", {"common", "medium", "rare"});
    index.add_document("b.md", {"common", "medium"});
    index.add_document("c.md", {"common", "medium"});
    index.add_document("d.md", {"common"});
    index.add_document("e.md", {"common"});

    const std::vector<std::string> expected{"a.md", "z.md"};
    loom::QueryEngine engine;
    EXPECT_EQ(paths_of(engine.query(index, "common medium rare")), expected);
    EXPECT_EQ(paths_of(engine.query(index, "rare common medium")), expected);
    EXPECT_EQ(paths_of(engine.query(index, "medium rare common rare")), expected);
    EXPECT_TRUE(engine.query(index, "common rare missing").empty());
}

TEST(QueryEngineTest, RanksDocumentsByTermFrequency) {
    loom::InvertedIndex index;
    index.add_document("a.md", std::unordered_map<std::string, std::size_t>{{"thread", 1}});
    index.add_document("b.md", std::unordered_map<std::string, std::size_t>{{"thread", 5}});
    index.add_document("c.md", std::unordered_map<std::string, std::size_t>{{"thread", 2}});

    const auto results = loom::QueryEngine{}.query(index, "thread");
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].path, "b.md");
    EXPECT_EQ(results[1].path, "c.md");
    EXPECT_EQ(results[2].path, "a.md");
    EXPECT_GT(results[0].score, results[1].score);
    EXPECT_GT(results[1].score, results[2].score);
}

TEST(QueryEngineTest, RareTermHasHigherIdfWeightThanCommonTerm) {
    loom::InvertedIndex index;
    index.add_document("a.md", std::unordered_map<std::string, std::size_t>{{"rare", 1}, {"common", 1}});
    index.add_document("b.md", std::unordered_map<std::string, std::size_t>{{"common", 5}});
    index.add_document("c.md", std::unordered_map<std::string, std::size_t>{{"common", 1}});
    index.add_document("d.md", std::unordered_map<std::string, std::size_t>{{"common", 1}});
    index.add_document("e.md", std::unordered_map<std::string, std::size_t>{{"rare", 1}});

    const auto rare_results = loom::QueryEngine{}.query(index, "rare");
    const auto common_results = loom::QueryEngine{}.query(index, "common");

    ASSERT_EQ(rare_results.size(), 2u);
    ASSERT_EQ(common_results.size(), 4u);

    // 单次 rare (df=2) 的得分应显著高于单次 common (df=4) 的得分
    const auto score_rare_1 = rare_results[0].score;
    double score_common_1 = 0.0;
    for (const auto& r : common_results) {
        if (r.path == "c.md") {
            score_common_1 = r.score;
            break;
        }
    }
    EXPECT_GT(score_rare_1, score_common_1);
}

TEST(QueryEngineTest, StableSortBreaksTiesByPathAlphabetically) {
    loom::InvertedIndex index;
    index.add_document("z.md", std::unordered_map<std::string, std::size_t>{{"thread", 2}});
    index.add_document("a.md", std::unordered_map<std::string, std::size_t>{{"thread", 2}});
    index.add_document("m.md", std::unordered_map<std::string, std::size_t>{{"thread", 2}});

    const auto results = loom::QueryEngine{}.query(index, "thread");
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].path, "a.md");
    EXPECT_EQ(results[1].path, "m.md");
    EXPECT_EQ(results[2].path, "z.md");
    EXPECT_DOUBLE_EQ(results[0].score, results[1].score);
    EXPECT_DOUBLE_EQ(results[1].score, results[2].score);
}
