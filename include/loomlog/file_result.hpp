#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace loom {

struct FileResult {
    std::string path;
    std::unordered_map<std::string, std::size_t> words;  // word → 词频 (TF)
    std::size_t bytes{0};
    bool success{false};
    std::string error;
};

}  // namespace loom
