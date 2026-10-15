#include "loomlog/text_analyzer.hpp"

#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace loom {
namespace {

bool is_word_char(unsigned char ch) {
    return std::isalnum(ch) != 0;
}

char to_lower_ascii(unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
}

template <class OnWord>
void for_each_word(std::string_view text, OnWord&& on_word) {
    std::string current;
    
    for (const unsigned char ch : text) {
        if (is_word_char(ch)) {
            current.push_back(to_lower_ascii(ch));
            continue;
        }

        if (!current.empty()) {
            on_word(std::exchange(current, {}));
        }
    } 

    if (!current.empty()) {
        on_word(std::move(current));
    }
}

}  // namespace

std::unordered_set<std::string> tokenize_words(std::string_view text) {
    std::unordered_set<std::string> words;

    // 第一版只做 ASCII 分词：字母和数字组成词，其他字符都是分隔符。
    for_each_word(text, 
        [&words](std::string word) {
        words.insert(std::move(word));
        }
    );

    return words;
}

std::unordered_map<std::string, std::size_t> tokenize_with_frequency(std::string_view text) {
    std::unordered_map<std::string, std::size_t> freq;

    // 和 tokenize_words 共用同一个分词逻辑，区别是用 map 累加计数而非 set 去重。
    for_each_word(text, [&freq](std::string word) {
        ++freq[std::move(word)];
    });

    return freq;
}

std::vector<std::string> normalize_query_terms(std::string_view text) {
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    // 查询词保持用户输入顺序，同时去掉重复项，方便后续做稳定的 AND 查询。
    for_each_word(text, [&terms, &seen](std::string word) {
        if (seen.insert(word).second) {
            terms.push_back(std::move(word));
        }
    });

    return terms;
}

}  // namespace loom
