#pragma once

#include <string>

namespace loom {

// 检索结果项：包含命中文档路径以及相关度评分（TF-IDF 分数）。
struct QueryResult {
    std::string path;
    double score{0.0};

    bool operator==(const QueryResult& other) const = default;
};

}  // namespace loom
