#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace loom {

using IndexSnapshot = std::vector<std::pair<std::string, std::vector<std::string>>>;

class InvertedIndex {
public:
    void add_document(const std::string& path, const std::unordered_map<std::string, std::size_t>& words);
    void add_document(const std::string& path, const std::unordered_set<std::string>& words);
    void add_document(const std::string& path, std::initializer_list<std::string> words);

    std::vector<std::string> find(const std::string& word) const;
    std::size_t term_frequency(const std::string& word, const std::string& path) const;
    IndexSnapshot snapshot() const;

    std::size_t keyword_count() const noexcept;
    std::size_t document_count() const noexcept;

private:
    struct PostingList {
        std::vector<std::string> docs;                    // 构建期只追加，不排序
        std::unordered_map<std::string, std::size_t> tf;  // path -> 词频 (TF)，兼做 O(1) 去重
    };

    std::unordered_map<std::string, PostingList> index_;
    std::unordered_set<std::string> documents_;
};

}  // namespace loom
