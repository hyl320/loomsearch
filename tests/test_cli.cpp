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

TEST(CliTest, SqliteIndexAndBenchmarkProcessBeyondInFlightWindow) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window";
    const auto db_path = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window.db";
    const auto output = std::filesystem::temp_directory_path() / "loomsearch_cli_sqlite_window.out";
    std::filesystem::remove_all(root);
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
