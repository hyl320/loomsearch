#include "loomlog/index_store.hpp"
#include "loomlog/indexer.hpp"
#include "loomlog/logger.hpp"
#include "loomlog/query_engine.hpp"
#include "loomlog/directory_scanner.hpp"
#include "loomlog/sqlite_store.hpp"
#include "loomlog/sqlite_indexer.hpp"
#include "loomlog/continuous_indexer.hpp"
#include "loomlog/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <deque>
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

template <class Consume>
void process_raw_files(
    const std::vector<std::filesystem::path>& files,
    loom::ThreadPool& pool,
    Consume consume
) {
    constexpr std::size_t max_in_flight = 32;
    std::deque<std::future<RawFile>> futures;
    std::size_t next_file = 0;
    const auto submit_next = [&] {
        futures.push_back(pool.submit([file = files[next_file]] {
            return read_raw(file);
        }));
        ++next_file;
    };

    while (next_file < files.size() && futures.size() < max_in_flight) {
        submit_next();
    }
    while (!futures.empty()) {
        auto future = std::move(futures.front());
        futures.pop_front();
        consume(future);
        if (next_file < files.size()) {
            submit_next();
        }
    }
}

long long elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();
}

long long percentile_ns(std::vector<long long> samples, std::size_t percentile) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto rank = (percentile * samples.size() + 99) / 100;
    return samples[rank - 1];
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

struct QueryCliOptions {
    std::string query_text;
    std::size_t limit{0};
    std::unordered_set<std::string> types;
};

QueryCliOptions parse_query_options(int argc, char* argv[], std::string_view command_name) {
    if (argc < 4) {
        throw std::invalid_argument(std::string(command_name) + " requires a target file and keywords");
    }

    QueryCliOptions options;
    std::ostringstream query_words;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--limit") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--limit requires a value");
            }
            options.limit = parse_size(argv[++i], "--limit");
        } else if (arg == "--type") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--type requires a value");
            }
            const std::string val(argv[++i]);
            std::stringstream ss(val);
            std::string item;
            while (std::getline(ss, item, ',')) {
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
                if (item.empty()) continue;
                if (!item.starts_with('.')) {
                    item = "." + item;
                }
                std::ranges::transform(item, item.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                options.types.insert(item);
            }
        } else if (arg.starts_with("--")) {
            throw std::invalid_argument("unknown option for " + std::string(command_name) + ": " + std::string(arg));
        } else {
            if (query_words.tellp() > 0) {
                query_words << ' ';
            }
            query_words << arg;
        }
    }

    options.query_text = query_words.str();
    if (options.query_text.empty()) {
        throw std::invalid_argument(std::string(command_name) + " requires at least one keyword");
    }

    return options;
}

bool matches_type_filter(const std::string& path, const std::unordered_set<std::string>& types) {
    if (types.empty()) {
        return true;
    }
    auto ext = std::filesystem::path(path).extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return types.contains(ext);
}

int run_query(int argc, char* argv[]) {
    const auto options = parse_query_options(argc, argv, "query");

    const std::filesystem::path index_path(argv[2]);
    if (!std::filesystem::exists(index_path)) {
        throw std::invalid_argument("index file does not exist: " + index_path.string());
    }

    const auto index = loom::IndexStore{}.load(index_path);
    auto matches = loom::QueryEngine{}.query(index, options.query_text);

    if (!options.types.empty()) {
        std::vector<loom::QueryResult> filtered;
        for (auto& match : matches) {
            if (matches_type_filter(match.path, options.types)) {
                filtered.push_back(std::move(match));
            }
        }
        matches = std::move(filtered);
    }

    if (options.limit > 0 && matches.size() > options.limit) {
        matches.resize(options.limit);
    }

    // 输出保持简单稳定，便于脚本消费和测试断言。
    std::cout << "matches: " << matches.size() << '\n';
    for (const auto& match : matches) {
        std::cout << match.path << '\n';
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

    static_cast<void>(thread_count);
    const auto started = std::chrono::steady_clock::now();
    const auto stats = loom::SqliteIndexer{}.build({root, output});

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();

    std::cout << "command: sqlite-index\n";
    std::cout << "files_total: " << stats.files_total << '\n';
    std::cout << "files_success: " << stats.files_updated << '\n';
    std::cout << "files_failed: " << stats.files_failed << '\n';
    std::cout << "files_read: " << stats.files_read << '\n';
    std::cout << "files_unchanged: " << stats.files_unchanged << '\n';
    std::cout << "files_removed: " << stats.files_removed << '\n';
    std::cout << "elapsed_ms: " << elapsed_ms << '\n';
    std::cout << "output: " << output.lexically_normal().string() << '\n';

    return stats.files_failed == 0 ? 0 : 1;
}

int run_sqlite_query(int argc, char* argv[]) {
    const auto options = parse_query_options(argc, argv, "sqlite-query");

    const std::filesystem::path db_path(argv[2]);
    if (!std::filesystem::exists(db_path)) {
        throw std::invalid_argument("database file does not exist: " + db_path.string());
    }

    loom::SqliteStore store;
    store.open(db_path);
    auto hits = store.search(options.query_text);

    if (!options.types.empty()) {
        std::vector<loom::SqliteSearchHit> filtered;
        for (auto& hit : hits) {
            if (matches_type_filter(hit.path, options.types)) {
                filtered.push_back(std::move(hit));
            }
        }
        hits = std::move(filtered);
    }

    if (options.limit > 0 && hits.size() > options.limit) {
        hits.resize(options.limit);
    }

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

    long long sqlite_build_ms = 0;
    {
        loom::SqliteStore store;
        store.open(db);
        store.begin_transaction();
        process_raw_files(files, pool, [&](std::future<RawFile>& future) {
            auto raw = future.get();
            if (raw.success) {
                store.insert(raw.path, raw.text);
            }
        });
        store.commit_transaction();
        sqlite_build_ms = elapsed_ms(sqlite_started);
        store.close();
    }
    pool.shutdown();
    const auto sqlite_bytes = std::filesystem::file_size(db);

    // 查询对比：同一批高频词构成单词查询和相邻词的 AND 查询。
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
    std::vector<long long> hand_single_ns, sqlite_single_ns;
    std::vector<long long> hand_and_ns, sqlite_and_ns;
    std::size_t mismatches = 0;

    const auto measure = [&](const std::string& term, bool multi_term) {
        const auto hand_query_started = std::chrono::steady_clock::now();
        const auto hand_hits = engine.query(hand.index, term);
        const auto hand_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - hand_query_started
        ).count();

        const auto sqlite_query_started = std::chrono::steady_clock::now();
        const auto sqlite_hits = query_store.search(term);
        const auto sqlite_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - sqlite_query_started
        ).count();
        if (multi_term) {
            hand_and_ns.push_back(hand_ns);
            sqlite_and_ns.push_back(sqlite_ns);
        } else {
            hand_single_ns.push_back(hand_ns);
            sqlite_single_ns.push_back(sqlite_ns);
            hand_query_us += hand_ns / 1000;
            sqlite_query_us += sqlite_ns / 1000;
        }

        std::set<std::string> hand_set;
        for (const auto& hit : hand_hits) {
            hand_set.insert(hit.path);
        }
        std::set<std::string> sqlite_set;
        for (const auto& hit : sqlite_hits) {
            sqlite_set.insert(hit.path);
        }
        if (hand_set != sqlite_set) {
            ++mismatches;
        }
    };

    for (std::size_t i = 0; i < query_limit; ++i) {
        measure(snapshot[i].first, false);
        if (snapshot.size() > 1) {
            measure(snapshot[i].first + " " + snapshot[(i + 1) % snapshot.size()].first, true);
        }
    }

    std::cout << "command: bench-search\n";
    std::cout << "corpus_files: " << files.size() << '\n';
    std::cout << "corpus_bytes: " << hand.stats.bytes_total << '\n';
    std::cout << "queries: " << query_limit << '\n';
    std::cout << "and_queries: " << hand_and_ns.size() << '\n';
    std::cout << "hand_index_build_ms: " << hand_build_ms << '\n';
    std::cout << "sqlite_index_build_ms: " << sqlite_build_ms << '\n';
    std::cout << "hand_index_bytes: " << hand_bytes << '\n';
    std::cout << "sqlite_index_bytes: " << sqlite_bytes << '\n';
    std::cout << "hand_query_total_us: " << hand_query_us << '\n';
    std::cout << "sqlite_query_total_us: " << sqlite_query_us << '\n';
    std::cout << "hand_single_p50_ns: " << percentile_ns(hand_single_ns, 50) << '\n';
    std::cout << "hand_single_p95_ns: " << percentile_ns(hand_single_ns, 95) << '\n';
    std::cout << "sqlite_single_p50_ns: " << percentile_ns(sqlite_single_ns, 50) << '\n';
    std::cout << "sqlite_single_p95_ns: " << percentile_ns(sqlite_single_ns, 95) << '\n';
    std::cout << "hand_and_p50_ns: " << percentile_ns(hand_and_ns, 50) << '\n';
    std::cout << "hand_and_p95_ns: " << percentile_ns(hand_and_ns, 95) << '\n';
    std::cout << "sqlite_and_p50_ns: " << percentile_ns(sqlite_and_ns, 50) << '\n';
    std::cout << "sqlite_and_p95_ns: " << percentile_ns(sqlite_and_ns, 95) << '\n';
    std::cout << "parity_mismatches: " << mismatches << '\n';
    return 0;
}

std::atomic<bool> g_stop_requested{false};
loom::ContinuousIndexer* g_active_indexer{nullptr};

void handle_signal(int signal) {
    (void)signal;
    g_stop_requested.store(true);
    if (g_active_indexer != nullptr) {
        g_active_indexer->stop();
    }
}

int run_watch(int argc, char* argv[]) {
    if (argc < 4) {
        throw std::invalid_argument("watch requires <directory> and <db-file>");
    }

    const std::filesystem::path root = argv[2];
    const std::filesystem::path db_file = argv[3];
    std::size_t debounce_ms = 100;
    std::size_t max_size_mb = 10;

    for (int i = 4; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--debounce" && i + 1 < argc) {
            debounce_ms = parse_size(argv[++i], "--debounce");
        } else if (arg == "--max-size" && i + 1 < argc) {
            max_size_mb = parse_size(argv[++i], "--max-size");
        } else {
            throw std::invalid_argument("unknown watch option: " + std::string(arg));
        }
    }

    loom::ContinuousIndexerOptions options;
    options.root = root;
    options.database = db_file;
    options.debounce_delay = std::chrono::milliseconds(debounce_ms);
    options.max_file_size = max_size_mb * 1024 * 1024;

    loom::ContinuousIndexer indexer;
    std::string error;
    if (!indexer.start(options, &error)) {
        throw std::runtime_error(error);
    }

    std::cout << "command: watch\n";
    std::cout << "root: " << root.string() << '\n';
    std::cout << "database: " << db_file.string() << '\n';
    std::cout << "debounce_ms: " << debounce_ms << '\n';
    std::cout << "max_size_mb: " << max_size_mb << '\n';
    std::cout << "status: watching (press Ctrl+C to exit)\n";
    std::cout.flush();

    g_active_indexer = &indexer;
    g_stop_requested.store(false);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while (!g_stop_requested.load() && indexer.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    indexer.stop();
    g_active_indexer = nullptr;

    const auto stats = indexer.stats();
    std::cout << "\nstatus: stopped\n";
    std::cout << "events_received: " << stats.events_received << '\n';
    std::cout << "files_indexed: " << stats.files_indexed << '\n';
    std::cout << "files_removed: " << stats.files_removed << '\n';
    return 0;
}

void print_usage() {
    std::cout << "usage:\n";
    std::cout << "  loomsearch index <directory> [--threads N] [--output PATH]\n";
    std::cout << "  loomsearch query <index-file> <keywords...> [--limit N] [--type EXT]\n";
    std::cout << "  loomsearch sqlite-index <directory> [--threads N] [--output PATH]\n";
    std::cout << "  loomsearch sqlite-query <db-file> <query...> [--limit N] [--type EXT]\n";
    std::cout << "  loomsearch watch <directory> <db-file> [--debounce MS] [--max-size MB]\n";
    std::cout << "  loomsearch sqlite-watch <directory> <db-file> [--debounce MS] [--max-size MB]\n";
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
        if (std::string_view(argv[1]) == "watch" || std::string_view(argv[1]) == "sqlite-watch") {
            return run_watch(argc, argv);
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
