#include "loomlog/query_engine.hpp"

#include "loomlog/text_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace loom {

std::vector<QueryResult> QueryEngine::query(
    const InvertedIndex& index,
    std::string_view text
) const {
    const auto terms = normalize_query_terms(text);
    if (terms.empty()) {
        return {};
    }

    const auto total_docs = index.document_count();
    if (total_docs == 0) {
        return {};
    }

    // 收集所有关键词的命中列表，若任一关键词无匹配则 AND 结果必为空
    std::vector<std::pair<std::string, std::vector<std::string>>> term_postings;
    term_postings.reserve(terms.size());
    for (const auto& term : terms) {
        auto docs = index.find(term);
        if (docs.empty()) {
            return {};
        }
        term_postings.emplace_back(term, std::move(docs));
    }

    // 按 posting 长度升序排序，减少求交集时的比较次数
    std::sort(term_postings.begin(), term_postings.end(), [](const auto& left, const auto& right) {
        return left.second.size() < right.second.size();
    });

    auto matches = term_postings.front().second;
    for (std::size_t i = 1; i < term_postings.size(); ++i) {
        std::vector<std::string> intersection;
        std::set_intersection(
            matches.begin(),
            matches.end(),
            term_postings[i].second.begin(),
            term_postings[i].second.end(),
            std::back_inserter(intersection)
        );
        matches = std::move(intersection);

        if (matches.empty()) {
            return {};
        }
    }

    // 预先计算每个查询词的 IDF：ln(1.0 + N / df)
    std::vector<std::pair<std::string, double>> term_idfs;
    term_idfs.reserve(terms.size());
    for (const auto& [term, docs] : term_postings) {
        const double df = static_cast<double>(docs.size());
        const double idf = std::log(1.0 + static_cast<double>(total_docs) / df);
        term_idfs.emplace_back(term, idf);
    }

    // 计算交集中每个文档的 TF-IDF 综合得分
    std::vector<QueryResult> results;
    results.reserve(matches.size());
    for (auto& doc : matches) {
        double score = 0.0;
        for (const auto& [term, idf] : term_idfs) {
            const auto tf = index.term_frequency(term, doc);
            score += static_cast<double>(tf) * idf;
        }
        results.push_back(QueryResult{std::move(doc), score});
    }

    // 结果按相关度降序排列；若分数相同则按路径字典序升序排列，保证结果确定稳定
    std::sort(results.begin(), results.end(), [](const QueryResult& left, const QueryResult& right) {
        if (std::abs(left.score - right.score) > 1e-9) {
            return left.score > right.score;
        }
        return left.path < right.path;
    });

    return results;
}

}  // namespace loom
