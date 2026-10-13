#include "loomlog/query_engine.hpp"

#include <gtest/gtest.h>
#include <vector>

TEST(QueryEngineTest, SingleKeywordReturnsMatchingDocuments) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"logger"});

    EXPECT_EQ(loom::QueryEngine{}.query(index, "THREAD"), std::vector<std::string>{"a.md"});
}

TEST(QueryEngineTest, MultipleKeywordsUseAndSemantics) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"thread"});
    index.add_document("c.md", {"pool"});

    EXPECT_EQ(loom::QueryEngine{}.query(index, "thread pool"), std::vector<std::string>{"a.md"});
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
    EXPECT_EQ(engine.query(index, "common medium rare"), expected);
    EXPECT_EQ(engine.query(index, "rare common medium"), expected);
    EXPECT_EQ(engine.query(index, "medium rare common rare"), expected);
    EXPECT_TRUE(engine.query(index, "common rare missing").empty());
}
