#pragma once


#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace loom {

// FTS5 命中结果。rank 是 SQLite 的 bm25() 分数（越小越相关），已经按相关度升序返回。
struct SqliteSearchHit {
    std::string path;
    double rank{0.0};
};

// SQLite FTS5 检索后端的存储与查询封装。
//
// 设计取舍：
// - 内部只持有一个连接，构建期在主线程批量写入，避免 SQLite 多线程写冲突。
// - 使用 pimpl 把 sqlite3 句柄藏在实现里，调用方无需包含 sqlite3.h。
class SqliteStore {
public:
    SqliteStore();
    ~SqliteStore();

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    SqliteStore(SqliteStore&&) noexcept;
    SqliteStore& operator=(SqliteStore&&) noexcept;

    // 打开（不存在则创建）数据库，并确保 FTS5 表结构存在。
    void open(const std::filesystem::path& path);
    void close();

    // 批量写入的显式事务边界，用于构建期把多次 insert 包成一个事务。
    void begin_transaction();
    void commit_transaction();

    // 把一份文档全文加入 FTS5 索引。path 只存不索引，content 参与检索。
    void insert(std::string_view path, std::string_view content);

    // 执行 FTS5 MATCH 查询，结果按 BM25 相关度从高到低返回。
    std::vector<SqliteSearchHit> search(std::string_view query) const;

    // 已索引的文档数。
    std::size_t document_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace loom
