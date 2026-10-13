#include "loomlog/sqlite_indexer.hpp"

#include "loomlog/directory_scanner.hpp"

#include <algorithm>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace loom {
namespace {

std::int64_t write_time_value(const std::filesystem::path& path) {
    return static_cast<std::int64_t>(std::filesystem::last_write_time(path).time_since_epoch().count());
}

std::string content_hash(std::string_view content) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : content) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}

bool read_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    if (file.bad()) return false;
    content = stream.str();
    return true;
}

std::string normalized_root(const std::filesystem::path& root) {
    return std::filesystem::absolute(root).lexically_normal().string();
}

}  // namespace

SqliteIndexStats SqliteIndexer::build(const SqliteIndexOptions& options) const {
    std::error_code root_error;
    if (!std::filesystem::is_directory(options.root, root_error) || root_error) {
        throw std::invalid_argument("sqlite index root is not an accessible directory: " + options.root.string());
    }
    const auto files = DirectoryScanner{}.scan(options.root);
    SqliteStore store;
    store.open(options.database);
    const auto root = normalized_root(options.root);
    const auto stored_root = store.index_root();
    if (stored_root.has_value() && *stored_root != root) {
        throw std::invalid_argument("sqlite index database belongs to a different root: " + *stored_root);
    }
    const auto existing = store.file_states();
    std::unordered_map<std::string, SqliteFileState> states;
    states.reserve(existing.size());
    for (auto state : existing) states.emplace(state.path, std::move(state));

    SqliteIndexStats stats;
    stats.files_total = files.size();
    store.begin_transaction();
    if (!stored_root.has_value()) store.set_index_root(root);
    for (const auto& file : files) {
        const auto path = file.lexically_normal().string();
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec) {
            ++stats.files_failed;
            continue;
        }
        const auto write_time = write_time_value(file);
        const auto found = states.find(path);
        if (found != states.end() && found->second.file_size == size
            && found->second.last_write_time == write_time) {
            ++stats.files_unchanged;
            continue;
        }

        std::string content;
        if (!read_file(file, content)) {
            ++stats.files_failed;
            continue;
        }
        ++stats.files_read;
        const auto hash = content_hash(content);
        const SqliteFileState state{path, size, write_time, hash};
        if (found == states.end()) {
            // update 也覆盖旧版本数据库中可能已经存在的同路径文档。
            store.update(path, content);
            ++stats.files_updated;
        } else if (found->second.content_hash != hash) {
            store.update(path, content);
            ++stats.files_updated;
        }
        store.upsert_file_state(state);
    }
    for (const auto& [path, state] : states) {
        const auto file = std::filesystem::path(path);
        if (std::ranges::find(files, file) != files.end()) continue;
        store.remove(path);
        store.remove_file_state(path);
        ++stats.files_removed;
    }
    store.commit_transaction();
    return stats;
}

}  // namespace loom
