#include "loomlog/index_store.hpp"
#include "loomlog/indexer.hpp"
#include "loomlog/logger.hpp"
#include "loomlog/query_engine.hpp"
#include "loomlog/directory_scanner.hpp"
#include "loomlog/sqlite_store.hpp"
#include "loomlog/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::size_t parse_size(std::string_view value, std::string_view option) {
    try {
        std::size_t parsed = 0;
        const auto number = std::stoull(std::string(value), &parsed);
        if (parsed != value.size() || number == 0) {
            throw std::invalid_argument("invalid value");
        }
        return static_cast<std::size_t>(number);
    } catch (const std::exception&) {
        throw std::invalid_argument(
            "invalid value for " + std::string(option) + ": " + std::string(value)
        );
    }
}

struct RawFile {
    std::string path;
    std::string text;
    std::size_t bytes{0};
    bool success{false};
    std::string error;
};

// 只读原文、不做分词：FTS5 后端用自己的 unicode61 分词，正好和手写 ASCII 分词形成对比。
RawFile read_raw(const std::filesystem::path& path) {
    RawFile result;
    result.path = path.lexically_normal().string();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "cannot open file: " + result.path;
        return result;
    }

    std::ostringstream content;
    content << file.rdbuf();
    if (file.bad()) {
        result.error = "cannot read file: " + result.path;
        return result;
    }

    result.text = content.str();
    result.bytes = result.text.size();
    result.success = true;
    return result;
}

long long elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();
}

std::size_t lcg_next(std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::size_t>(state >> 33);
}

int run_bench_pool(int argc, char* argv[]) {
    std::size_t thread_count = 4;
    std::size_t task_count = 100'000;

    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--threads" || option == "--jobs") && i + 1 < argc) {
            const auto value = parse_size(argv[++i], option);
            if (option == "--threads") {
                thread_count = value;
            } else {
                task_count = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    loom::ThreadPool pool(thread_count);
    std::atomic<std::size_t> completed{0};
    std::vector<std::future<void>> futures;
    futures.reserve(task_count);

    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < task_count; ++i) {
        futures.push_back(pool.submit([&completed] {
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    pool.shutdown();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    std::cout << "command: bench-pool\n";
    std::cout << "threads: " << thread_count << '\n';
    std::cout << "jobs: " << task_count << '\n';
    std::cout << "completed: " << completed.load() << '\n';
    std::cout << "elapsed_ms: " << elapsed << '\n';

    return completed.load() == task_count ? 0 : 1;
}

int run_bench_log(int argc, char* argv[]) {
    std::size_t message_count = 100'000;
    std::string path =
        (std::filesystem::temp_directory_path() / "loomlog_cli_bench.log").string();

    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--messages" || option == "--file") && i + 1 < argc) {
            const std::string value(argv[++i]);
            if (option == "--messages") {
                message_count = parse_size(value, option);
            } else {
                path = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    std::filesystem::remove(path);

    loom::LoggerConfig config;
    config.path = path;
    config.queue_limit = message_count + 1;
    config.batch_size = 256;
    config.rotate_bytes = 0;

    loom::Logger logger(config);
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < message_count; ++i) {
        logger.info("INFO message {}", i);
    }

    const auto submit_done = std::chrono::steady_clock::now();
    logger.shutdown();
    const auto finished = std::chrono::steady_clock::now();

    const auto submit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        submit_done - start
    ).count();
    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        finished - start
    ).count();

    std::cout << "command: bench-log\n";
    std::cout << "messages: " << message_count << '\n';
    std::cout << "submit_ms: " << submit_ms << '\n';
    std::cout << "total_ms: " << total_ms << '\n';
    std::cout << "dropped: " << logger.dropped() << '\n';
    std::cout << "file: " << path << '\n';
    return 0;
}

int run_grep(int argc, char* argv[]) {
    std::string path;
    std::string keyword;
    std::string level;

    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--file" || option == "--keyword" || option == "--level")
            && i + 1 < argc) {
            const std::string value(argv[++i]);
            if (option == "--file") {
                path = value;
            } else if (option == "--keyword") {
                keyword = value;
            } else {
                level = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    if (path.empty()) {
        throw std::invalid_argument("grep requires --file");
    }

    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open file: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!keyword.empty() && line.find(keyword) == std::string::npos) {
            continue;
        }
        if (!level.empty() && line.find(level) == std::string::npos) {
            continue;
        }
        std::cout << line << '\n';
    }

    return 0;
}

int run_index(int argc, char* argv[]) {
    //有没有漏参数
    if (argc < 3) {
        throw std::invalid_argument("index requires a directory");
    }
    //是否是已近存在的目录
    const std::filesystem::path root(argv[2]);
    if (!std::filesystem::is_directory(root)) {
        throw std::invalid_argument("index requires an existing directory: " + root.string());
    }
    
    //默认配置是否是线程数和输出文件
    std::size_t thread_count = 0;
    std::filesystem::path output = "index.bin";
    
    //如果当前参数是 --threads 或 --output，并且后面还有值，就读取下一个参数作为它的值。
    for (int i = 3; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--threads" || option == "--output") && i + 1 < argc) {
            const std::string value(argv[++i]);
            if (option == "--threads") {
                thread_count = parse_size(value, option);
            } else {
                output = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    loom::LoggerConfig logger_config;
    logger_config.path = "loomsearch.log";
    loom::Logger logger(logger_config);

    loom::IndexBuildOptions options;
    options.root = root;
    options.thread_count = thread_count;
    options.logger = &logger;

    // CLI 是索引构建的调度入口：并发处理由 Indexer 内部交给 ThreadPool。
    // 1. 并发扫描 + 构建内存索引
    const auto result = loom::Indexer{}.build(options);
    //2. 把索引落盘
    loom::IndexStore{}.save(result.index, output);
    // 3. 优雅关闭日志线程
    logger.shutdown();

    std::cout << "command: index\n";
    std::cout << "files_total: " << result.stats.files_total << '\n';
    std::cout << "files_success: " << result.stats.files_success << '\n';
    std::cout << "files_failed: " << result.stats.files_failed << '\n';
    std::cout << "bytes_total: " << result.stats.bytes_total << '\n';
    std::cout << "keywords_total: " << result.stats.keywords_total << '\n';
    std::cout << "elapsed_ms: " << result.stats.elapsed_ms << '\n';
    std::cout << "output: " << output.lexically_normal().string() << '\n';

    return result.stats.files_failed == 0 ? 0 : 1;
}

int run_query(int argc, char* argv[]) {
    if (argc < 4) {
        throw std::invalid_argument("query requires an index file and keywords");
    }

    const std::filesystem::path index_path(argv[2]);
    std::ostringstream query_text;
    for (int i = 3; i < argc; ++i) {
        if (i > 3) {
            query_text << ' ';
        }
        query_text << argv[i];
    }

    const auto index = loom::IndexStore{}.load(index_path);
    const auto matches = loom::QueryEngine{}.query(index, query_text.str());

    // 输出保持简单稳定，便于脚本消费和测试断言。
    std::cout << "matches: " << matches.size() << '\n';
    for (const auto& match : matches) {
        std::cout << match << '\n';
    }

    return 0;
}

int run_sqlite_index(int argc, char* argv[]) {
    if (argc < 3) {
        throw std::invalid_argument("sqlite-index requires a directory");
    }

    const std::filesystem::path root(argv[2]);
    if (!std::filesystem::is_directory(root)) {
        throw std::invalid_argument(
            "sqlite-index requires an existing directory: " + root.string()
        );
    }

    std::size_t thread_count = 0;
    std::filesystem::path output = "index.db";

    for (int i = 3; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--threads" || option == "--output") && i + 1 < argc) {
            const std::string value(argv[++i]);
            if (option == "--threads") {
                thread_count = parse_size(value, option);
            } else {
                output = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    const auto started = std::chrono::steady_clock::now();
    const auto files = loom::DirectoryScanner{}.scan(root);

    loom::ThreadPool pool(thread_count);
    std::vector<std::future<RawFile>> futures;
    futures.reserve(files.size());
    for (auto& file : files) {
        futures.push_back(pool.submit([file = std::move(file)] {
            return read_raw(file);
        }));
    }

    // 重建语义：清掉上一次的索引，避免把新文档追加进旧库。
    std::error_code ec;
    std::filesystem::remove(output, ec);
    std::filesystem::remove(output.string() + "-wal", ec);
    std::filesystem::remove(output.string() + "-shm", ec);

    loom::SqliteStore store;
    store.open(output);
    store.begin_transaction();

    std::size_t files_success = 0;
    std::size_t files_failed = 0;
    std::size_t bytes_total = 0;

    for (auto& future : futures) {
        RawFile raw;
        try {
            raw = future.get();
        } catch (const std::exception&) {
            ++files_failed;
            continue;
        }
        if (!raw.success) {
            ++files_failed;
            continue;
        }
        bytes_total += raw.bytes;
        store.insert(raw.path, raw.text);
        ++files_success;
    }

    store.commit_transaction();
    store.close();
    pool.shutdown();

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();

    std::cout << "command: sqlite-index\n";
    std::cout << "files_total: " << files.size() << '\n';
    std::cout << "files_success: " << files_success << '\n';
    std::cout << "files_failed: " << files_failed << '\n';
    std::cout << "bytes_total: " << bytes_total << '\n';
    std::cout << "elapsed_ms: " << elapsed_ms << '\n';
    std::cout << "output: " << output.lexically_normal().string() << '\n';

    return files_failed == 0 ? 0 : 1;
}

int run_sqlite_query(int argc, char* argv[]) {
    if (argc < 4) {
        throw std::invalid_argument("sqlite-query requires a database file and keywords");
    }

    const std::filesystem::path db_path(argv[2]);
    std::ostringstream query_text;
    for (int i = 3; i < argc; ++i) {
        if (i > 3) {
            query_text << ' ';
        }
        query_text << argv[i];
    }

    loom::SqliteStore store;
    store.open(db_path);
    const auto hits = store.search(query_text.str());

    // 结果已经按 BM25 相关度排序，输出保持稳定便于脚本消费和测试断言。
    std::cout << "matches: " << hits.size() << '\n';
    for (const auto& hit : hits) {
        std::cout << hit.path << '\n';
    }

    return 0;
}

int run_corpus_gen(int argc, char* argv[]) {
    if (argc < 3) {
        throw std::invalid_argument("corpus-gen requires a directory");
    }

    const std::filesystem::path root(argv[2]);
    std::size_t file_count = 500;
    std::size_t words_per_file = 50;

    for (int i = 3; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--files" || option == "--words") && i + 1 < argc) {
            const auto value = parse_size(argv[++i], option);
            if (option == "--files") {
                file_count = value;
            } else {
                words_per_file = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    std::filesystem::create_directories(root);

    // 确定性语料：固定字典 + LCG，前 20 个词出现得更频繁，给 BM25 一个可展示的词频分布。
    constexpr std::size_t dict_size = 200;
    constexpr std::size_t common_size = 20;

    for (std::size_t f = 0; f < file_count; ++f) {
        std::uint64_t state = 123456789ULL + f * 2654435761ULL;
        const auto path = root / ("doc_" + std::to_string(f) + ".txt");
        std::ofstream out(path);
        for (std::size_t w = 0; w < words_per_file; ++w) {
            const auto r = lcg_next(state);
            const std::size_t idx =
                (r % 100) < 40 ? r % common_size : common_size + (r % (dict_size - common_size));
            out << "word" << idx;
            if (w + 1 != words_per_file) {
                out << ' ';
            }
        }
        out << '\n';
    }

    std::cout << "command: corpus-gen\n";
    std::cout << "directory: " << root.lexically_normal().string() << '\n';
    std::cout << "files: " << file_count << '\n';
    std::cout << "words_per_file: " << words_per_file << '\n';
    return 0;
}

int run_bench_search(int argc, char* argv[]) {
    if (argc < 3) {
        throw std::invalid_argument("bench-search requires a directory");
    }

    const std::filesystem::path root(argv[2]);
    if (!std::filesystem::is_directory(root)) {
        throw std::invalid_argument(
            "bench-search requires an existing directory: " + root.string()
        );
    }

    std::size_t thread_count = 0;
    std::size_t query_count = 20;
    for (int i = 3; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if ((option == "--threads" || option == "--queries") && i + 1 < argc) {
            const auto value = parse_size(argv[++i], option);
            if (option == "--threads") {
                thread_count = value;
            } else {
                query_count = value;
            }
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
    }

    const auto files = loom::DirectoryScanner{}.scan(root);
    if (files.empty()) {
        throw std::runtime_error("no files to index under: " + root.string());
    }

    // 手写后端：内存合并 + 落盘（两者共用同一个 files 列表，保证对比公平）。
    const auto hand_started = std::chrono::steady_clock::now();
    const auto hand = loom::Indexer{}.build_files(files, thread_count, nullptr);
    const auto bin = std::filesystem::temp_directory_path() / "loomsearch_bench_index.bin";
    loom::IndexStore{}.save(hand.index, bin);
    const auto hand_build_ms = elapsed_ms(hand_started);
    const auto hand_bytes = std::filesystem::file_size(bin);

    // SQLite FTS5 后端：并行读原文 + 主线程一个事务批量写入。
    const auto db = std::filesystem::temp_directory_path() / "loomsearch_bench_index.db";
    std::error_code ec;
    std::filesystem::remove(db, ec);
    std::filesystem::remove(db.string() + "-wal", ec);
    std::filesystem::remove(db.string() + "-shm", ec);

    const auto sqlite_started = std::chrono::steady_clock::now();
    loom::ThreadPool pool(thread_count);
    std::vector<std::future<RawFile>> futures;
    futures.reserve(files.size());
    for (auto& file : files) {
        futures.push_back(pool.submit([file = std::move(file)] {
            return read_raw(file);
        }));
    }

    long long sqlite_build_ms = 0;
    {
        loom::SqliteStore store;
        store.open(db);
        store.begin_transaction();
        for (auto& future : futures) {
            auto raw = future.get();
            if (raw.success) {
                store.insert(raw.path, raw.text);
            }
        }
        store.commit_transaction();
        sqlite_build_ms = elapsed_ms(sqlite_started);
        store.close();
    }
    pool.shutdown();
    const auto sqlite_bytes = std::filesystem::file_size(db);

    // 查询对比：取手写索引里文档数最多的前 K 个单词作为共享查询集，逐条计时并核对结果集。
    auto snapshot = hand.index.snapshot();
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.second.size() > right.second.size();
    });
    const auto query_limit = std::min<std::size_t>(query_count, snapshot.size());

    loom::QueryEngine engine;
    loom::SqliteStore query_store;
    query_store.open(db);

    long long hand_query_us = 0;
    long long sqlite_query_us = 0;
    std::size_t mismatches = 0;

    for (std::size_t i = 0; i < query_limit; ++i) {
        const std::string& term = snapshot[i].first;

        const auto hand_query_started = std::chrono::steady_clock::now();
        const auto hand_hits = engine.query(hand.index, term);
        hand_query_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - hand_query_started
        ).count();

        const auto sqlite_query_started = std::chrono::steady_clock::now();
        const auto sqlite_hits = query_store.search(term);
        sqlite_query_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - sqlite_query_started
        ).count();

        const std::set<std::string> hand_set(hand_hits.begin(), hand_hits.end());
        std::set<std::string> sqlite_set;
        for (const auto& hit : sqlite_hits) {
            sqlite_set.insert(hit.path);
        }
        if (hand_set != sqlite_set) {
            ++mismatches;
        }
    }

    std::cout << "command: bench-search\n";
    std::cout << "corpus_files: " << files.size() << '\n';
    std::cout << "corpus_bytes: " << hand.stats.bytes_total << '\n';
    std::cout << "queries: " << query_limit << '\n';
    std::cout << "hand_index_build_ms: " << hand_build_ms << '\n';
    std::cout << "sqlite_index_build_ms: " << sqlite_build_ms << '\n';
    std::cout << "hand_index_bytes: " << hand_bytes << '\n';
    std::cout << "sqlite_index_bytes: " << sqlite_bytes << '\n';
    std::cout << "hand_query_total_us: " << hand_query_us << '\n';
    std::cout << "sqlite_query_total_us: " << sqlite_query_us << '\n';
    std::cout << "parity_mismatches: " << mismatches << '\n';
    return 0;
}

void print_usage() {
    std::cout << "usage:\n";
    std::cout << "  loomsearch index <directory> [--threads N] [--output PATH]\n";
    std::cout << "  loomsearch query <index-file> <keywords...>\n";
    std::cout << "  loomsearch sqlite-index <directory> [--threads N] [--output PATH]\n";
    std::cout << "  loomsearch sqlite-query <db-file> <query...>\n";
    std::cout << "  loomsearch corpus-gen <directory> [--files N] [--words N]\n";
    std::cout << "  loomsearch bench-search <directory> [--threads N] [--queries N]\n";
    std::cout << "  loomsearch bench-pool [--threads N] [--jobs N]\n";
    std::cout << "  loomsearch bench-log [--messages N] [--file PATH]\n";
    std::cout << "  loomsearch grep --file PATH [--keyword TEXT] [--level LEVEL]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            print_usage();
            return argc < 2 ? 1 : 0;
        }

        if (std::string_view(argv[1]) == "bench-pool") {
            return run_bench_pool(argc, argv);
        }
        if (std::string_view(argv[1]) == "bench-log") {
            return run_bench_log(argc, argv);
        }
        if (std::string_view(argv[1]) == "grep") {
            return run_grep(argc, argv);
        }
        if (std::string_view(argv[1]) == "index") {
            return run_index(argc, argv);
        }
        if (std::string_view(argv[1]) == "query") {
            return run_query(argc, argv);
        }
        if (std::string_view(argv[1]) == "sqlite-index") {
            return run_sqlite_index(argc, argv);
        }
        if (std::string_view(argv[1]) == "sqlite-query") {
            return run_sqlite_query(argc, argv);
        }
        if (std::string_view(argv[1]) == "corpus-gen") {
            return run_corpus_gen(argc, argv);
        }
        if (std::string_view(argv[1]) == "bench-search") {
            return run_bench_search(argc, argv);
        }

        throw std::invalid_argument("unknown command: " + std::string(argv[1]));
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        print_usage();
        return 2;
    }
}
