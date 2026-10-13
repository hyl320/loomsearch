#include "loomlog/query_engine.hpp"

#include "loomlog/text_analyzer.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace loom {

std::vector<std::string> QueryEngine::query(
    const InvertedIndex& index,
    std::string_view text
) const {
    const auto terms = normalize_query_terms(text);
    if (terms.empty()) {
        return {};
    }

    if (terms.size() == 1) {
        return index.find(terms.front());
    }

    std::vector<std::vector<std::string>> postings;
    postings.reserve(terms.size());
    for (const auto& term : terms) {
        auto docs = index.find(term);
        if (docs.empty()) {
            return {};
        }
        postings.push_back(std::move(docs));
    }
    std::sort(postings.begin(), postings.end(), [](const auto& left, const auto& right) {
        return left.size() < right.size();
    });

    auto matches = std::move(postings.front());
    for (std::size_t i = 1; i < postings.size(); ++i) {
        std::vector<std::string> intersection;
        std::set_intersection(
            matches.begin(),
            matches.end(),
            postings[i].begin(),
            postings[i].end(),
            std::back_inserter(intersection)
        );
        matches = std::move(intersection);

        if (matches.empty()) {
            break;
        }
    }

    return matches;
}

}  // namespace loom
