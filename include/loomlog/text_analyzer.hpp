#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace loom {

std::unordered_set<std::string> tokenize_words(std::string_view text);

// 分词并统计每个词的出现次数（词频），用于构建带 TF 信息的倒排索引。
std::unordered_map<std::string, std::size_t> tokenize_with_frequency(std::string_view text);

std::vector<std::string> normalize_query_terms(std::string_view text);

}  // namespace loom
