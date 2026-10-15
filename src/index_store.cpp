#include "loomlog/index_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace loom {
namespace {

constexpr auto kMagic = "LOOMSEARCH_INDEX_V1";

std::size_t parse_count(const std::string& line, const std::string& field) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(line, &consumed);
        if (consumed != line.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return static_cast<std::size_t>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid index count field: " + field);
    }
}

std::string read_required_line(std::istream& input, const std::string& field) {
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("unexpected end of index while reading: " + field);
    }
    return line;
}

}  // namespace

void IndexStore::save(const InvertedIndex& index, const std::filesystem::path& path) const {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // 原子保存：先写至带时间戳的临时影子文件，刷盘完整后再通过操作系统原子重命名替换
    const auto tmp_path = path.string() + ".tmp."
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    {
        std::ofstream output(tmp_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open index for write: " + tmp_path);
        }

        // 文本格式保持可读和稳定；snapshot 已按关键词和路径排序。
        const auto snapshot = index.snapshot();
        output << kMagic << '\n';
        output << snapshot.size() << '\n';
        for (const auto& [word, docs] : snapshot) {
            output << word << '\n';
            output << docs.size() << '\n';
            for (const auto& doc : docs) {
                output << doc << '\n';
            }
        }
        output.flush();
        if (!output) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_path, rm_ec);
            throw std::runtime_error("failed to flush index file: " + tmp_path);
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        throw std::runtime_error("cannot rename temporary index file to: " + path.string() + " (" + ec.message() + ")");
    }
}

InvertedIndex IndexStore::load(const std::filesystem::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open index for read: " + path.string());
    }

    // 先校验 magic header，避免把普通文本误当作索引继续解析。
    const auto magic = read_required_line(input, "magic");
    if (magic != kMagic) {
        throw std::runtime_error("invalid index header");
    }

    InvertedIndex index;
    const auto keyword_count = parse_count(
        read_required_line(input, "keyword count"),
        "keyword count"
    );

    for (std::size_t i = 0; i < keyword_count; ++i) {
        const auto word = read_required_line(input, "keyword");
        const auto doc_count = parse_count(
            read_required_line(input, "document count"),
            "document count"
        );

        for (std::size_t j = 0; j < doc_count; ++j) {
            const auto doc = read_required_line(input, "document path");
            index.add_document(doc, std::unordered_set<std::string>{word});
        }
    }

    return index;
}

}  // namespace loom
