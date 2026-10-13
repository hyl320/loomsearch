#include "loomlog/sqlite_store.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace loom {
namespace {

[[noreturn]] void throw_db_error(sqlite3* db, const std::string& context) {
    const char* message = db != nullptr ? sqlite3_errmsg(db) : "database is not open";
    throw std::runtime_error(context + ": " + message);
}

void exec_or_throw(sqlite3* db, const char* sql, const std::string& context) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message =
            context + ": " + (error != nullptr ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql, const std::string& context) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw_db_error(db, context);
        }
    }

    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const noexcept { return stmt_; }

    void bind_text(int index, std::string_view value, const std::string& context) {
        if (sqlite3_bind_text(
                stmt_,
                index,
                value.data(),
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT
            ) != SQLITE_OK) {
            throw_db_error(db_, context);
        }
    }

    void step_done(const std::string& context) {
        const int result = sqlite3_step(stmt_);
        if (result != SQLITE_DONE) {
            throw_db_error(db_, context);
        }
    }

    void reset_for_reuse() {
        if (sqlite3_reset(stmt_) != SQLITE_OK) {
            throw_db_error(db_, "cannot reset insert");
        }
        if (sqlite3_clear_bindings(stmt_) != SQLITE_OK) {
            throw_db_error(db_, "cannot clear insert bindings");
        }
    }

    void discard_result() noexcept {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

private:
    sqlite3* db_{nullptr};
    sqlite3_stmt* stmt_{nullptr};
};

}  // namespace

struct SqliteStore::Impl {
    sqlite3* db{nullptr};
    std::unique_ptr<Statement> insert_statement;
};

SqliteStore::SqliteStore() : impl_(std::make_unique<Impl>()) {}

SqliteStore::~SqliteStore() {
    if (impl_ != nullptr) {
        close();
    }
}

SqliteStore::SqliteStore(SqliteStore&&) noexcept = default;
SqliteStore& SqliteStore::operator=(SqliteStore&& other) noexcept {
    if (this != &other) {
        if (impl_ != nullptr) {
            close();
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void SqliteStore::open(const std::filesystem::path& path) {
    close();

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(path.string().c_str(), &impl_->db, flags, nullptr) != SQLITE_OK) {
        throw_db_error(impl_->db, "cannot open sqlite database");
    }

    // WAL 让读写并发更友好，也是 SQLite 上常见的默认推荐。
    exec_or_throw(impl_->db, "PRAGMA journal_mode=WAL;", "cannot enable WAL");
    exec_or_throw(
        impl_->db,
        "CREATE VIRTUAL TABLE IF NOT EXISTS documents USING fts5(path UNINDEXED, content);",
        "cannot create fts5 table"
    );
    impl_->insert_statement = std::make_unique<Statement>(
        impl_->db,
        "INSERT INTO documents(path, content) VALUES(?1, ?2);",
        "cannot prepare insert"
    );
}

void SqliteStore::close() {
    impl_->insert_statement.reset();
    if (impl_->db != nullptr) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

void SqliteStore::begin_transaction() {
    exec_or_throw(impl_->db, "BEGIN TRANSACTION;", "cannot begin transaction");
}

void SqliteStore::commit_transaction() {
    exec_or_throw(impl_->db, "COMMIT;", "cannot commit transaction");
}

void SqliteStore::insert(std::string_view path, std::string_view content) {
    auto& statement = *impl_->insert_statement;
    try {
        statement.bind_text(1, path, "cannot bind path");
        statement.bind_text(2, content, "cannot bind content");
        statement.step_done("cannot insert document");
        statement.reset_for_reuse();
    } catch (...) {
        statement.discard_result();
        throw;
    }
}

std::vector<SqliteSearchHit> SqliteStore::search(std::string_view query) const {
    if (query.empty()) {
        return {};
    }

    Statement statement(
        impl_->db,
        "SELECT path, bm25(documents) AS rank "
        "FROM documents WHERE documents MATCH ?1 ORDER BY rank;",
        "cannot prepare search"
    );
    statement.bind_text(1, query, "cannot bind query");

    std::vector<SqliteSearchHit> hits;
    while (true) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_ROW) {
            const auto* path = reinterpret_cast<const char*>(
                sqlite3_column_text(statement.get(), 0)
            );
            SqliteSearchHit hit;
            if (path != nullptr) {
                hit.path = path;
            }
            hit.rank = sqlite3_column_double(statement.get(), 1);
            hits.push_back(std::move(hit));
            continue;
        }
        if (result == SQLITE_DONE) {
            break;
        }
        throw_db_error(impl_->db, "cannot search documents");
    }

    return hits;
}

std::size_t SqliteStore::document_count() const {
    Statement statement(
        impl_->db,
        "SELECT count(*) FROM documents;",
        "cannot prepare count"
    );
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw_db_error(impl_->db, "cannot count documents");
    }
    return static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 0));
}

}  // namespace loom
