# LoomSearch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build LoomSearch, a C++20 thread-pool based parallel file indexer with persistent inverted indexes and query support.

**Architecture:** Existing `loom::ThreadPool` runs file processing tasks, each worker returns `FileResult`, and the main indexing thread merges successful results into `InvertedIndex`. `IndexStore` persists a stable text index format, `QueryEngine` performs normalized single-keyword and multi-keyword AND queries, and `Logger` records task lifecycle events.

**Tech Stack:** C++20, CMake 3.20+, GoogleTest, standard library filesystem/thread/future containers.

**Spec:** `docs/superpowers/specs/2026-09-16-loomsearch-design.md`

## Global Constraints

- Namespace remains `loom`.
- Supported indexed extensions are exactly `.txt`, `.md`, `.cpp`, `.hpp`.
- Worker tasks must not mutate shared `InvertedIndex`; they return `FileResult`.
- The first index format is text and starts with `LOOMSEARCH_INDEX_V1`.
- Query semantics are AND across all normalized query terms.
- Key implementation comments are written in Chinese.
- Old CLI debug commands `bench-pool`, `bench-log`, and `grep` remain available.
- No GUI, HTTP service, database, fuzzy search, BM25, incremental indexing, file watcher, distributed indexing, lock-free queue, or compression.

---

## File Structure

- Create `include/loomlog/file_result.hpp`: value type returned by file-processing workers.
- Create `include/loomlog/text_analyzer.hpp`: shared word normalization/tokenization API used by processors and queries.
- Create `src/text_analyzer.cpp`: ASCII tokenizer and query term normalizer.
- Create `include/loomlog/file_processor.hpp`: file-to-`FileResult` interface.
- Create `src/file_processor.cpp`: reads files, counts bytes, tokenizes content, reports errors in `FileResult`.
- Create `include/loomlog/directory_scanner.hpp`: supported-extension scanning API.
- Create `src/directory_scanner.cpp`: recursive scan with extension filtering.
- Create `include/loomlog/inverted_index.hpp`: document merge and lookup API.
- Create `src/inverted_index.cpp`: deterministic snapshot and keyword lookup.
- Create `include/loomlog/index_store.hpp`: save/load persistent index.
- Create `src/index_store.cpp`: text format writer/reader with validation.
- Create `include/loomlog/query_engine.hpp`: query API over loaded index.
- Create `src/query_engine.cpp`: normalized AND query implementation.
- Create `include/loomlog/indexer.hpp`: orchestration types and public index build API.
- Create `src/indexer.cpp`: scan, submit worker tasks, merge results, log task status.
- Modify `src/cli.cpp`: add `index` and `query` commands and rename usage to `loomsearch`.
- Modify `CMakeLists.txt`: add new sources, test executables, and CLI target output name.
- Create tests for each new module and end-to-end behavior.
- Modify `DESIGN.md`: replace old thread-pool-only design with Chinese LoomSearch design.
- Create or modify `README.md`: Chinese usage, build, architecture, verification notes.
- Modify `.gitignore`: ignore build outputs, logs, temporary index files, and editor folders.

---

### Task 1: FileResult And Text Analyzer

**Files:**
- Create: `include/loomlog/file_result.hpp`
- Create: `include/loomlog/text_analyzer.hpp`
- Create: `src/text_analyzer.cpp`
- Create: `tests/test_text_analyzer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `loom::FileResult`, `loom::tokenize_words(std::string_view) -> std::unordered_set<std::string>`, `loom::normalize_query_terms(std::string_view) -> std::vector<std::string>`

- [ ] **Step 1: Write the failing tokenizer tests**

```cpp
#include "loomlog/text_analyzer.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

TEST(TextAnalyzerTest, TokenizesAsciiWordsAsLowercaseUniqueTerms) {
    const auto words = loom::tokenize_words("Thread_pool THREAD logger.md 42");
    const std::unordered_set<std::string> expected{
        "thread", "pool", "logger", "md", "42"
    };
    EXPECT_EQ(words, expected);
}

TEST(TextAnalyzerTest, NormalizesQueryTermsInInputOrderWithoutDuplicates) {
    const auto terms = loom::normalize_query_terms("Thread pool THREAD");
    const std::vector<std::string> expected{"thread", "pool"};
    EXPECT_EQ(terms, expected);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_text_analyzer`

Expected: build fails because `loomlog/text_analyzer.hpp` does not exist or target is not defined.

- [ ] **Step 3: Implement minimal code**

Add `FileResult` exactly as:

```cpp
namespace loom {

struct FileResult {
    std::string path;
    std::unordered_set<std::string> words;
    std::size_t bytes{0};
    bool success{false};
    std::string error;
};

}  // namespace loom
```

Add tokenizer functions that treat ASCII letters and digits as word characters, lowercase letters with `std::tolower`, split on all other characters, and remove duplicate query terms while preserving first-seen order.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_text_analyzer && build/test_text_analyzer.exe`

Expected: both tokenizer tests pass.

---

### Task 2: FileProcessor

**Files:**
- Create: `include/loomlog/file_processor.hpp`
- Create: `src/file_processor.cpp`
- Create: `tests/test_file_processor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `loom::FileResult`, `loom::tokenize_words`
- Produces: `loom::FileProcessor::process(const std::filesystem::path&) -> FileResult`

- [ ] **Step 1: Write failing file processor tests**

```cpp
#include "loomlog/file_processor.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {
std::filesystem::path fresh_dir(std::string_view name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}
}

TEST(FileProcessorTest, ReadsTextAndExtractsLowercaseWords) {
    const auto dir = fresh_dir("loomsearch_file_processor_words");
    const auto path = dir / "sample.md";
    std::ofstream(path) << "Thread pool\nLOGGER thread";

    const auto result = loom::FileProcessor{}.process(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.path, path.lexically_normal().string());
    EXPECT_TRUE(result.words.contains("thread"));
    EXPECT_TRUE(result.words.contains("pool"));
    EXPECT_TRUE(result.words.contains("logger"));
}

TEST(FileProcessorTest, CountsBytesFromFileContent) {
    const auto dir = fresh_dir("loomsearch_file_processor_bytes");
    const auto path = dir / "sample.txt";
    std::ofstream(path, std::ios::binary) << "abc\n";

    const auto result = loom::FileProcessor{}.process(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.bytes, 4u);
}

TEST(FileProcessorTest, MissingFileReturnsFailureResult) {
    const auto dir = fresh_dir("loomsearch_file_processor_missing");
    const auto result = loom::FileProcessor{}.process(dir / "missing.txt");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_file_processor`

Expected: build fails because the processor header or target does not exist.

- [ ] **Step 3: Implement minimal code**

Implement `FileProcessor::process` with a Chinese comment before the file read explaining that workers produce independent results and do not touch shared index state. Use `std::ifstream` in binary mode, read into `std::ostringstream`, count string size in bytes, tokenize content, and return failure result on open/read exceptions.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_file_processor && build/test_file_processor.exe`

Expected: all file processor tests pass.

---

### Task 3: DirectoryScanner

**Files:**
- Create: `include/loomlog/directory_scanner.hpp`
- Create: `src/directory_scanner.cpp`
- Create: `tests/test_directory_scanner.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `loom::DirectoryScanner::scan(const std::filesystem::path&) -> std::vector<std::filesystem::path>`

- [ ] **Step 1: Write failing scanner tests**

```cpp
#include "loomlog/directory_scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(DirectoryScannerTest, RecursivelyFindsSupportedExtensions) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_scanner_supported";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    std::ofstream(root / "a.txt") << "a";
    std::ofstream(root / "b.md") << "b";
    std::ofstream(root / "nested" / "c.cpp") << "c";
    std::ofstream(root / "nested" / "d.hpp") << "d";
    std::ofstream(root / "image.png") << "skip";
    std::ofstream(root / "data.bin") << "skip";

    const auto files = loom::DirectoryScanner{}.scan(root);

    std::vector<std::string> names;
    for (const auto& file : files) {
        names.push_back(file.filename().string());
    }
    std::ranges::sort(names);
    const std::vector<std::string> expected{"a.txt", "b.md", "c.cpp", "d.hpp"};
    EXPECT_EQ(names, expected);
}

TEST(DirectoryScannerTest, MissingRootReturnsEmptyList) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_scanner_missing";
    std::filesystem::remove_all(root);
    EXPECT_TRUE(loom::DirectoryScanner{}.scan(root).empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_directory_scanner`

Expected: build fails because scanner code is absent.

- [ ] **Step 3: Implement minimal code**

Use `std::filesystem::recursive_directory_iterator` with `std::error_code`, skip non-regular files, lowercase extension before comparison, sort returned paths lexicographically, and include a Chinese comment explaining supported-extension filtering.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_directory_scanner && build/test_directory_scanner.exe`

Expected: both scanner tests pass.

---

### Task 4: InvertedIndex

**Files:**
- Create: `include/loomlog/inverted_index.hpp`
- Create: `src/inverted_index.cpp`
- Create: `tests/test_inverted_index.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: document path and `std::unordered_set<std::string>`
- Produces: `add_document`, `find`, `snapshot`, `keyword_count`, `document_count`

- [ ] **Step 1: Write failing inverted index tests**

```cpp
#include "loomlog/inverted_index.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

TEST(InvertedIndexTest, AddsDocumentsForEachWord) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"pool", "logger"});

    EXPECT_EQ(index.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(index.find("logger"), std::vector<std::string>{"b.md"});
    EXPECT_EQ(index.find("pool"), (std::vector<std::string>{"a.md", "b.md"}));
}

TEST(InvertedIndexTest, DoesNotDuplicateSameDocumentForSameWord) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread"});
    index.add_document("a.md", {"thread"});

    EXPECT_EQ(index.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(index.document_count(), 1u);
}

TEST(InvertedIndexTest, SnapshotIsSortedByKeywordAndPath) {
    loom::InvertedIndex index;
    index.add_document("c.cpp", {"pool"});
    index.add_document("a.md", {"thread", "pool"});

    const auto snapshot = index.snapshot();

    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].first, "pool");
    EXPECT_EQ(snapshot[0].second, (std::vector<std::string>{"a.md", "c.cpp"}));
    EXPECT_EQ(snapshot[1].first, "thread");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_inverted_index`

Expected: build fails because index code is absent.

- [ ] **Step 3: Implement minimal code**

Store `std::unordered_map<std::string, std::vector<std::string>> index_` and `std::unordered_set<std::string> documents_`. On add, skip duplicate document paths per keyword; sort vectors after insertion or during snapshot. Add a Chinese comment explaining that this class is intentionally single-threaded in v1.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_inverted_index && build/test_inverted_index.exe`

Expected: all inverted index tests pass.

---

### Task 5: IndexStore

**Files:**
- Create: `include/loomlog/index_store.hpp`
- Create: `src/index_store.cpp`
- Create: `tests/test_index_store.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `loom::InvertedIndex`
- Produces: `loom::IndexStore::save(const InvertedIndex&, const std::filesystem::path&)`, `loom::IndexStore::load(const std::filesystem::path&) -> InvertedIndex`

- [ ] **Step 1: Write failing persistence tests**

```cpp
#include "loomlog/index_store.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

TEST(IndexStoreTest, SavesAndLoadsIndex) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_roundtrip";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";

    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"pool"});

    loom::IndexStore{}.save(index, path);
    const auto loaded = loom::IndexStore{}.load(path);

    EXPECT_EQ(loaded.find("thread"), std::vector<std::string>{"a.md"});
    EXPECT_EQ(loaded.find("pool"), (std::vector<std::string>{"a.md", "b.md"}));
}

TEST(IndexStoreTest, RejectsBadHeader) {
    const auto dir = std::filesystem::temp_directory_path() / "loomsearch_index_store_bad_header";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "index.bin";
    std::ofstream(path) << "BAD\n";

    EXPECT_THROW(loom::IndexStore{}.load(path), std::runtime_error);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_index_store`

Expected: build fails because store code is absent.

- [ ] **Step 3: Implement minimal code**

Write magic header, keyword count, each keyword, document count, and document paths. Load validates header and integer lines with `std::stoull`, rebuilds `InvertedIndex` by adding each keyword as a one-element word set per document path. Add Chinese comments around format validation.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_index_store && build/test_index_store.exe`

Expected: persistence tests pass.

---

### Task 6: QueryEngine

**Files:**
- Create: `include/loomlog/query_engine.hpp`
- Create: `src/query_engine.cpp`
- Create: `tests/test_query_engine.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `loom::InvertedIndex`, `loom::normalize_query_terms`
- Produces: `loom::QueryEngine::query(const InvertedIndex&, std::string_view) -> std::vector<std::string>`

- [ ] **Step 1: Write failing query tests**

```cpp
#include "loomlog/query_engine.hpp"

#include <gtest/gtest.h>

TEST(QueryEngineTest, SingleKeywordReturnsMatchingDocuments) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"logger"});

    EXPECT_EQ(loom::QueryEngine{}.query(index, "THREAD"), std::vector<std::string>{"a.md"});
}

TEST(QueryEngineTest, MultipleKeywordsUseAndSemantics) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread", "pool"});
    index.add_document("b.md", {"thread"});
    index.add_document("c.md", {"pool"});

    EXPECT_EQ(loom::QueryEngine{}.query(index, "thread pool"), std::vector<std::string>{"a.md"});
}

TEST(QueryEngineTest, EmptyQueryReturnsNoMatches) {
    loom::InvertedIndex index;
    index.add_document("a.md", {"thread"});

    EXPECT_TRUE(loom::QueryEngine{}.query(index, "!!!").empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_query_engine`

Expected: build fails because query code is absent.

- [ ] **Step 3: Implement minimal code**

Normalize query terms, return empty for no terms, start with first term's document list, then intersect with each following term using sorted vectors. Add a Chinese comment explaining AND semantics.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_query_engine && build/test_query_engine.exe`

Expected: query tests pass.

---

### Task 7: Indexer

**Files:**
- Create: `include/loomlog/indexer.hpp`
- Create: `src/indexer.cpp`
- Create: `tests/test_indexer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `DirectoryScanner`, `ThreadPool`, `FileProcessor`, `InvertedIndex`, optional `Logger`
- Produces: `loom::IndexBuildOptions`, `loom::IndexBuildStats`, `loom::IndexBuildResult`, `loom::Indexer::build(const IndexBuildOptions&) -> IndexBuildResult`

- [ ] **Step 1: Write failing indexer tests**

```cpp
#include "loomlog/indexer.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

TEST(IndexerTest, BuildsIndexFromSupportedFilesInParallel) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_parallel";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "a.md") << "thread pool";
    std::ofstream(root / "b.txt") << "thread logger";
    std::ofstream(root / "skip.png") << "thread pool";

    loom::IndexBuildOptions options;
    options.root = root;
    options.thread_count = 2;

    const auto result = loom::Indexer{}.build(options);

    EXPECT_EQ(result.stats.files_total, 2u);
    EXPECT_EQ(result.stats.files_success, 2u);
    EXPECT_EQ(result.stats.files_failed, 0u);
    EXPECT_EQ(result.index.find("thread").size(), 2u);
    EXPECT_EQ(result.index.find("pool"), std::vector<std::string>{(root / "a.md").lexically_normal().string()});
}

TEST(IndexerTest, EmptyDirectoryProducesEmptyIndex) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_indexer_empty";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    loom::IndexBuildOptions options;
    options.root = root;
    options.thread_count = 2;

    const auto result = loom::Indexer{}.build(options);

    EXPECT_EQ(result.stats.files_total, 0u);
    EXPECT_EQ(result.index.keyword_count(), 0u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_indexer`

Expected: build fails because indexer code is absent.

- [ ] **Step 3: Implement minimal code**

Define stats fields `files_total`, `files_success`, `files_failed`, `bytes_total`, `keywords_total`, `elapsed_ms`. In `build`, scan files, submit each file to `ThreadPool`, collect futures, merge only successful `FileResult` values, and log start/completion/failure if `options.logger` is non-null. Add Chinese comments at the submit loop and merge loop explaining the concurrency boundary.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_indexer && build/test_indexer.exe`

Expected: indexer tests pass.

---

### Task 8: CLI Index And Query Commands

**Files:**
- Modify: `src/cli.cpp`
- Create: `tests/test_cli.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Indexer`, `IndexStore`, `QueryEngine`, `Logger`
- Produces: CLI commands `index` and `query`

- [ ] **Step 1: Write failing CLI tests**

```cpp
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

TEST(CliTest, IndexAndQueryRoundTrip) {
    const auto root = std::filesystem::temp_directory_path() / "loomsearch_cli_docs";
    const auto index_path = std::filesystem::temp_directory_path() / "loomsearch_cli_index.bin";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "thread_pool.txt") << "thread pool";
    std::ofstream(root / "logger.md") << "thread logger";

    const auto index_command =
        "build\\\\loomsearch.exe index \"" + root.string() + "\" --threads 2 --output \"" + index_path.string() + "\"";
    ASSERT_EQ(std::system(index_command.c_str()), 0);

    const auto query_command =
        "build\\\\loomsearch.exe query \"" + index_path.string() + "\" \"thread pool\" > \"" +
        (root / "query.out").string() + "\"";
    ASSERT_EQ(std::system(query_command.c_str()), 0);

    std::ifstream output(root / "query.out");
    std::stringstream buffer;
    buffer << output.rdbuf();
    const auto text = buffer.str();
    EXPECT_NE(text.find("matches: 1"), std::string::npos);
    EXPECT_NE(text.find("thread_pool.txt"), std::string::npos);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target test_cli`

Expected: target does not exist or CLI output name is not `loomsearch`.

- [ ] **Step 3: Implement minimal code**

Add `run_index` and `run_query`. Parse `--threads` and `--output` for `index`; parse index path and all remaining terms for `query`. Create `Logger` with path `loomsearch.log` for indexing, call `IndexStore::save`, print stats, load/query for query command, print `matches: N` and each path. Keep old commands unchanged.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target loomsearch test_cli && build/test_cli.exe`

Expected: CLI round-trip test passes.

---

### Task 9: Documentation And Verification

**Files:**
- Modify: `DESIGN.md`
- Create or modify: `README.md`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: completed CLI and module behavior
- Produces: Chinese project documentation

- [ ] **Step 1: Update root design document**

Write Chinese sections for project goal, architecture, module responsibilities, data flow, concurrency boundary, index format, error handling, Logger role, and non-goals.

- [ ] **Step 2: Update README**

Write Chinese sections for one-sentence positioning, build commands, usage examples, supported file types, output examples, old debug commands, verification commands, and current limitations.

- [ ] **Step 3: Update ignore rules**

Ignore build folders, generated executables, logs, temp indexes, editor folders, and OS metadata.

- [ ] **Step 4: Run full verification**

Run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target loomsearch
```

Then run a manual CLI round trip:

```powershell
New-Item -ItemType Directory -Force -Path .\out\loomsearch-demo
Set-Content -Path .\out\loomsearch-demo\thread_pool.txt -Value 'thread pool worker'
Set-Content -Path .\out\loomsearch-demo\logger.md -Value 'thread logger'
.\build\loomsearch.exe index .\out\loomsearch-demo --threads 2 --output .\out\loomsearch-demo\index.bin
.\build\loomsearch.exe query .\out\loomsearch-demo\index.bin "thread pool"
```

Expected: tests pass, CLI builds, query output includes `matches: 1` and `thread_pool.txt`.

---

## Self-Review

- Spec coverage: every requirement in the design document maps to Tasks 1 through 9.
- Placeholder scan: this plan avoids open-ended placeholder language and gives concrete files, interfaces, test bodies, and verification commands.
- Type consistency: `FileResult`, `tokenize_words`, `normalize_query_terms`, `FileProcessor`, `DirectoryScanner`, `InvertedIndex`, `IndexStore`, `QueryEngine`, and `Indexer` names are used consistently across tasks.
