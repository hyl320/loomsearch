#pragma once

#include "loomlog/inverted_index.hpp"
#include "loomlog/query_result.hpp"

#include <string_view>
#include <vector>

namespace loom {

class QueryEngine {
public:
    // 执行关键词检索（多词隐式 AND），并按 TF-IDF 相关度得分降序、路径字典序升序返回结果。
    std::vector<QueryResult> query(const InvertedIndex& index, std::string_view text) const;
};

}  // namespace loom
