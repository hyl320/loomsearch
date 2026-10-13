#pragma once

#include "loomlog/sqlite_store.hpp"

#include <cstddef>
#include <filesystem>

namespace loom {

struct SqliteIndexOptions {
    std::filesystem::path root;
    std::filesystem::path database;
};

struct SqliteIndexStats {
    std::size_t files_total{0};
    std::size_t files_read{0};
    std::size_t files_updated{0};
    std::size_t files_unchanged{0};
    std::size_t files_failed{0};
    std::size_t files_removed{0};
};

class SqliteIndexer {
public:
    SqliteIndexStats build(const SqliteIndexOptions& options) const;
};

}  // namespace loom
