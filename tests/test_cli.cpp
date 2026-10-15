#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string quote_text(const std::string& text) {
    return "\"" + text + "\"";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string cmd_wrap(const std::string& command) {
#ifdef _WIN32
    return "cmd /C \"" + command + "\"";
#else
    return command;
#endif
}

}  // namespace

TEST(CliTest, IndexAndQueryRoundTrip) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_docs";
    const auto index_path = std::filesystem::temp_directory_path() / "loomsearch_cli_index.bin";
    const auto query_output = root / "query.out";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "thread_pool.txt") << "thread pool";
    std::ofstream(root / "logger.md") << "thread logger";

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " index " + quote(root) + " --threads 2 --output " + quote(index_path)
    );

    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command = cmd_wrap(
        cli + " query " + quote(index_path) + " " + quote_text("thread pool") +
        " > " + quote(query_output)
    );

    ASSERT_EQ(std::system(query_command.c_str()), 0);

    const auto text = read_file(query_output);
    EXPECT_NE(text.find("matches: 1"), std::string::npos);
    EXPECT_NE(text.find("thread_pool.txt"), std::string::npos);
    EXPECT_EQ(text.find("logger.md"), std::string::npos);
}

TEST(CliTest, SqliteIndexAndQueryRoundTrip) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_docs";
    const auto db_path = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite.db";
    const auto query_output = root / "query.out";
    std::filesystem::remove_all(root);
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path.string() + "-wal");
    std::filesystem::remove(db_path.string() + "-shm");
    std::filesystem::create_directories(root);
    std::ofstream(root / "thread_pool.txt") << "thread pool worker";
    std::ofstream(root / "logger.md") << "thread logger";

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " sqlite-index " + quote(root) + " --threads 2 --output " + quote(db_path)
    );

    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command = cmd_wrap(
        cli + " sqlite-query " + quote(db_path) + " " + quote_text("thread pool") +
        " > " + quote(query_output)
    );

    ASSERT_EQ(std::system(query_command.c_str()), 0);

    const auto text = read_file(query_output);
    EXPECT_NE(text.find("matches: 1"), std::string::npos);
    EXPECT_NE(text.find("thread_pool.txt"), std::string::npos);
    EXPECT_EQ(text.find("logger.md"), std::string::npos);
}

TEST(CliTest, SqliteIndexReusesDatabaseIncrementally) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_incremental_docs";
    const auto db_path = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_incremental.db";
    const auto output = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_incremental.out";
    std::filesystem::remove_all(root);
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path.string() + "-wal");
    std::filesystem::remove(db_path.string() + "-shm");
    std::filesystem::create_directories(root);
    std::ofstream(root / "a.txt") << "alpha";
    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index = [&](const std::string& extra = "") {
        return cmd_wrap(cli + " sqlite-index " + quote(root) + " --output " + quote(db_path)
            + extra + " > " + quote(output));
    };

    ASSERT_EQ(std::system(index().c_str()), 0);
    EXPECT_NE(read_file(output).find("files_success: 1"), std::string::npos);
    ASSERT_EQ(std::system(index().c_str()), 0);
    EXPECT_NE(read_file(output).find("files_unchanged: 1"), std::string::npos);

    std::filesystem::remove(root / "a.txt");
    ASSERT_EQ(std::system(index().c_str()), 0);
    EXPECT_NE(read_file(output).find("files_removed: 1"), std::string::npos);
}

TEST(CliTest, SqliteIndexAndBenchmarkProcessBeyondInFlightWindow) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window";
    const auto db_path = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window.db";
    const auto output = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window.out";
    std::filesystem::remove_all(root);
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path.string() + "-wal");
    std::filesystem::remove(db_path.string() + "-shm");
    std::filesystem::create_directories(root);
    for (int i = 0; i < 70; ++i) {
        std::ofstream(root / ("doc_" + std::to_string(i) + ".txt")) << "thread pool";
    }

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " sqlite-index " + quote(root) + " --threads 4 --output " + quote(db_path) +
        " > " + quote(output)
    );
    ASSERT_EQ(std::system(index_command.c_str()), 0);
    EXPECT_NE(read_file(output).find("files_success: 70"), std::string::npos);

    const auto query_command = cmd_wrap(
        cli + " sqlite-query " + quote(db_path) + " thread > " + quote(output)
    );
    ASSERT_EQ(std::system(query_command.c_str()), 0);
    EXPECT_NE(read_file(output).find("matches: 70"), std::string::npos);

    const auto bench_command = cmd_wrap(
        cli + " bench-search " + quote(root) + " --threads 4 --queries 2 > " + quote(output)
    );
    ASSERT_EQ(std::system(bench_command.c_str()), 0);
    const auto benchmark = read_file(output);
    EXPECT_NE(benchmark.find("corpus_files: 70"), std::string::npos);
    EXPECT_NE(benchmark.find("queries: 2\n"), std::string::npos);
    EXPECT_NE(benchmark.find("and_queries: 2\n"), std::string::npos);
    EXPECT_NE(benchmark.find("hand_single_p50_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("sqlite_single_p95_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("hand_and_p50_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("hand_and_p95_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("sqlite_and_p50_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("sqlite_and_p95_ns: "), std::string::npos);
    EXPECT_NE(benchmark.find("parity_mismatches: 0"), std::string::npos);
}

TEST(CliTest, BenchmarkWithOneTermHasNoAndQueries) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_one_term";
    const auto output = std::filesystem::temp_directory_path() / "loomsearch_cli_one_term.out";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "doc.txt") << "alpha";

    const auto command = cmd_wrap(
        quote(LOOMSEARCH_CLI_PATH) + " bench-search " + quote(root) +
        " --threads 1 --queries 1 > " + quote(output)
    );
    ASSERT_EQ(std::system(command.c_str()), 0);
    const auto benchmark = read_file(output);
    EXPECT_NE(benchmark.find("and_queries: 0\n"), std::string::npos);
    EXPECT_NE(benchmark.find("hand_and_p50_ns: 0\n"), std::string::npos);
    EXPECT_NE(benchmark.find("parity_mismatches: 0"), std::string::npos);
}

TEST(CliTest, QueryWithLimitRestrictsNumberOfMatches) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_limit_docs";
    const auto index_path = std::filesystem::temp_directory_path() / "loomsearch_cli_limit_index.bin";
    const auto output = root / "query.out";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "doc1.txt") << "common keyword";
    std::ofstream(root / "doc2.txt") << "common keyword";
    std::ofstream(root / "doc3.txt") << "common keyword";

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " index " + quote(root) + " --threads 1 --output " + quote(index_path)
    );
    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command = cmd_wrap(
        cli + " query " + quote(index_path) + " common --limit 2 > " + quote(output)
    );
    ASSERT_EQ(std::system(query_command.c_str()), 0);

    const auto text = read_file(output);
    EXPECT_NE(text.find("matches: 2"), std::string::npos);
}

TEST(CliTest, QueryWithTypeFiltersByExtension) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_type_docs";
    const auto index_path = std::filesystem::temp_directory_path() / "loomsearch_cli_type_index.bin";
    const auto output = root / "query.out";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "code.cpp") << "common thread";
    std::ofstream(root / "readme.md") << "common thread";

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " index " + quote(root) + " --threads 1 --output " + quote(index_path)
    );
    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command = cmd_wrap(
        cli + " query " + quote(index_path) + " common --type .cpp > " + quote(output)
    );
    ASSERT_EQ(std::system(query_command.c_str()), 0);

    const auto text = read_file(output);
    EXPECT_NE(text.find("matches: 1"), std::string::npos);
    EXPECT_NE(text.find("code.cpp"), std::string::npos);
    EXPECT_EQ(text.find("readme.md"), std::string::npos);
}

TEST(CliTest, SqliteQuerySupportsLimitAndType) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_limit_docs";
    const auto db_path = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_limit.db";
    const auto output = root / "query.out";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "code.cpp") << "search engine";
    std::ofstream(root / "note.txt") << "search engine";

    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    const auto index_command = cmd_wrap(
        cli + " sqlite-index " + quote(root) + " --output " + quote(db_path)
    );
    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command = cmd_wrap(
        cli + " sqlite-query " + quote(db_path) + " search --type .txt --limit 1 > " + quote(output)
    );
    ASSERT_EQ(std::system(query_command.c_str()), 0);

    const auto text = read_file(output);
    EXPECT_NE(text.find("matches: 1"), std::string::npos);
    EXPECT_NE(text.find("note.txt"), std::string::npos);
    EXPECT_EQ(text.find("code.cpp"), std::string::npos);
}

TEST(CliTest, QueryWithInvalidArgsReturnsNonZero) {
    const std::string cli = quote(LOOMSEARCH_CLI_PATH);
    // 缺少查询关键词
    const auto cmd_no_keyword = cmd_wrap(cli + " query non_existent_index.bin");
    EXPECT_NE(std::system(cmd_no_keyword.c_str()), 0);

    // --limit 传了非法值 0
    const auto cmd_invalid_limit = cmd_wrap(cli + " query dummy.bin keyword --limit 0");
    EXPECT_NE(std::system(cmd_invalid_limit.c_str()), 0);

    // 索引文件不存在
    const auto cmd_missing_file = cmd_wrap(cli + " query non_existent_file.bin keyword");
    EXPECT_NE(std::system(cmd_missing_file.c_str()), 0);
}
