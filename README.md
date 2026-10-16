# LoomSearch

LoomSearch 是一个基于 C++20 线程池的并行文件索引器：递归扫描目录，并发读取与分词，构建倒排索引，并从索引文件查询关键词。它同时内置 SQLite FTS5 增量索引后端，用于实际的可重复本地搜索，以及对比「自研倒排索引」和「工业级 FTS5」的真实取舍。

## 支持范围

默认只索引这些文件：

```text
.txt
.md
.cpp
.hpp
```

不做 GUI、HTTP 服务和模糊搜索。手写后端保持全量构建与原子持久化；SQLite 后端支持单次增量索引以及常驻后台的实时持续监听与防抖自动更新。

## 构建

依赖：CMake >= 3.20、支持 C++20 的编译器（GCC / Clang / MSVC）、Ninja（可选）。GoogleTest 由 CMake `FetchContent` 自动拉取；SQLite 以官方 amalgamation 形式 vendor 在 `third_party/sqlite/`，构建无需联网（仅首次拉取 gtest 需要网络）。

```powershell
cmake -S . -B build -DLOOMLOG_BUILD_CLI=ON
cmake --build build
```

或使用 preset：`cmake --preset debug` / `cmake --preset release`（构建产物在 `out/build/<preset>`）。

生成的可执行文件：

```text
build\loomsearch.exe     # Windows
build/loomsearch         # Linux / macOS
```

## CLI 命令总览

```text
loomsearch index <目录> [--threads N] [--output PATH]        # 手写倒排索引 (全量构建)
loomsearch query <索引文件> <关键词...> [--limit N] [--type EXT] # 多关键词 AND + TF-IDF 排名查询
loomsearch sqlite-index <目录> [--threads N] [--output PATH] # FTS5 增量建库 (单次批量)
loomsearch sqlite-query <数据库> <查询...> [--limit N] [--type EXT] # FTS5 查询（BM25/短语/前缀）
loomsearch watch <目录> <数据库> [--debounce MS] [--max-size MB]        # 实时常驻监控与自动增量更新
loomsearch sqlite-watch <目录> <数据库> [--debounce MS] [--max-size MB] # 实时监控对等别名
loomsearch corpus-gen <目录> [--files N] [--words N]         # 生成确定性 benchmark 语料
loomsearch bench-search <目录> [--threads N] [--queries N]   # 对比两个检索后端
loomsearch bench-pool [--threads N] [--jobs N]               # 线程池吞吐微基准
loomsearch bench-log [--messages N] [--file PATH]            # 异步日志写入微基准
loomsearch grep --file PATH [--keyword TEXT] [--level LEVEL] # 按关键词/级别过滤日志文件
```

运行 `loomsearch --help` 可查看全部命令和参数。更详细的查询语法与错误提示说明参见 [docs/QUERY_SYNTAX.md](docs/QUERY_SYNTAX.md)。

### 手写后端：index / query

建立索引：

```powershell
.\build\loomsearch.exe index .\documents --threads 8 --output .\index.bin
```

查询（支持多词隐式 AND、TF-IDF 相关度排序、文件类型过滤与条数限制）：

```powershell
# 基础多词查询（自动按 TF-IDF 相关度从高到低排序，同分按路径稳定排列）
.\build\loomsearch.exe query .\index.bin thread pool

# 限制最多返回 3 条最相关结果
.\build\loomsearch.exe query .\index.bin thread --limit 3

# 仅检索特定文件类型（如 .cpp 文件）
.\build\loomsearch.exe query .\index.bin thread --type .cpp

# 组合使用：过滤类型且限制返回条数
.\build\loomsearch.exe query .\index.bin thread pool --type .txt,.md --limit 5
```

输出示例：

```text
matches: 1
documents/thread_pool.md
```

### SQLite FTS5 后端：sqlite-index / sqlite-query

```powershell
.\build\loomsearch.exe sqlite-index .\documents --threads 8 --output .\index.db
.\build\loomsearch.exe sqlite-query .\index.db "thread pool"
```

同样支持 `--limit` 与 `--type` 过滤：

```powershell
.\build\loomsearch.exe sqlite-query .\index.db "thread pool" --type .md --limit 5
```

`sqlite-index` 将文件状态（路径、大小、修改时间、内容 hash）和 FTS5 文档保存在同一个数据库：首次执行建立完整索引；后续执行跳过未变化文件，只读取新增或修改文件，并删除已经从目录消失的文档。数据库会绑定首次使用的规范化绝对目录；用不同目录复用同一个数据库会被拒绝，避免误删原索引。

输出中的 `files_read`、`files_unchanged`、`files_removed` 可直接观察增量效果。例如仅修改一个文件时，预期 `files_read: 1`、`files_updated: 1`；没有变化时，预期 `files_read: 0`。

FTS5 额外提供手写索引没有的能力：

- **BM25 排名**：结果按相关度从高到低排序。
- **短语查询**：`sqlite-query .\index.db '"thread pool"'` 只命中相邻词组。
- **前缀查询**：`sqlite-query .\index.db 'thread*'` 命中 `thread`、`threads` 等。

### 实时常驻监控与增量更新：watch / sqlite-watch

启动长期运行的守护监听模式：

```powershell
.\build\loomsearch.exe watch .\documents .\index.db --debounce 100 --max-size 10
```

程序会在前台常驻运行，具备以下核心能力：
- **存量基线同步**：启动时自动完成当前目录现有文件的增量基线构建并自愈异常状态；
- **文件新建**：自动捕获并利用只读 `mmap` 分词入库；
- **文件修改**：自动比对哈希并更新索引；
- **文件删除**：自动从数据库抹除对应文档；
- **防抖合并**：短时间内高频修改（如编辑器保存时的临时文件抖动）自动合并为 1 次处理；
- **大文件限制**：超过 `--max-size`（默认 10MB）的文件自动跳过，防止内存耗尽；
- **优雅退出**：按 `Ctrl+C` 触发平滑停机，未完成事件自动清扫刷盘，保证数据库绝不损坏。

## 架构

```text
手写后端:
  index:
    DirectoryScanner -> ThreadPool -> FileProcessor (mmap) -> FileResult
                                        |
                                        v
                               InvertedIndex -> IndexStore (原子影子写入)
  query:
    IndexStore -> QueryEngine -> matches

SQLite FTS5 后端:
  sqlite-index:
    DirectoryScanner -> 文件状态比较 -> 只读变化文件 -> SqliteStore (FTS5 + 状态表)
  sqlite-query:
    SqliteStore (FTS5 MATCH + BM25) -> matches
  watch (常驻监听):
    FileWatcher (原生 OS 事件) -> EventDebouncer (防抖合并) -> ContinuousIndexer -> SqliteStore
```

手写后端的 worker 线程只负责读取和解析文件、返回 `FileResult`，不直接修改共享索引；主线程统一合并结果，减少锁竞争，也让并发边界更容易解释和测试。持久化采用影子临时文件刷盘与原子重命名，防止进程崩溃损坏旧索引。

## 测试与质量

运行全部测试（共 95 项）：

```powershell
ctest --test-dir build --output-on-failure
```

只跑 CLI 往返测试：

```powershell
.\build\test_cli.exe
```

CI（`.github/workflows/ci.yml`）在 Linux 上跑 ASan+UBSan 与 TSan。Windows 本机验证中，文件监听测试 `ContinuousIndexerTest.AutoIndexesNewFiles` 偶尔因事件时序竞争失败；单独重复运行通常通过，其余测试稳定通过。

## Benchmark

`corpus-gen` 使用固定字典 + 线性同余发生器生成语料，相同参数输出完全确定；`bench-search` 用同一批文件对比手写索引和 FTS5 的构建耗时、索引体积与查询耗时，并核对结果集是否一致（`parity_mismatches`）。查询耗时现在额外输出单词语句和双词 AND 的 p50/p95 纳秒分位数。

一键复现：

```powershell
.\build\loomsearch.exe corpus-gen .\out\corpus --files 2000 --words 80
.\build\loomsearch.exe bench-search .\out\corpus --threads 4 --queries 30
```

或直接运行矩阵脚本 `scripts/benchmark.ps1`（Linux/macOS 用 `scripts/benchmark.sh`）。它会组合不同文件数、每文件词数和线程数，重复运行并采集手写/SQLite 两条后端的构建耗时、查询 p50/p95 和峰值工作集，结果写入 CSV：

```powershell
.\scripts\benchmark.ps1 -Bin .\build\loomsearch.exe -Corpus .\out\benchmark-corpora -Output .\out\benchmark.csv
```

阶段一的优化前后对比、结论与限制记录在 [docs/benchmarks/phase-1.md](docs/benchmarks/phase-1.md)。注意：脚本里的 `first`/`repeat` 只表示同语料下的首次与后续进程，并非严格清空 OS 文件缓存后的冷/热缓存测量。

本机一次运行（Intel64 Family 6 Model 154，4 线程，30 个查询，结果只用于观察量级、不做跨机器结论）：

```text
手写索引    build 271 ms, 索引 3.2 MB, query 总计 28.8 ms
SQLite FTS5 build 178 ms, 索引 2.1 MB, query 总计 165.2 ms
parity_mismatches: 0
```

## 设计取舍

- worker 只读取、分词并返回 `FileResult`，主线程统一合并到 `InvertedIndex`。共享索引本身不需要加锁，并发边界清晰，也更容易通过 TSan。
- 手写后端和 SQLite FTS5 后端并存：手写索引胜在无依赖、可解释、内存热路径查询快；FTS5 胜在构建更快、磁盘更小，并提供 BM25、短语和前缀查询。两者在同一语料上返回相同结果集，benchmark 才能真实对比「自研轮子」和「工业级方案」的取舍。
- 手写后端用平滑 TF-IDF 排序，FTS5 用 BM25。BM25 额外引入词频饱和与文档长度归一化，避免高频词和大文档天然占便宜。
- 手写索引持久化采用影子临时文件刷盘后再原子 rename，防止进程中途退出损坏旧索引；SQLite 后端用 WAL 事务批量写入，并记录 `schema_version` / `build_status` 用于异常恢复。
- `watch` 链路为 `FileWatcher -> EventDebouncer -> ContinuousIndexer -> SqliteStore`：原生 OS 事件被防抖合并后批量更新，普通文件用只读 mmap 零拷贝分词，超过 `--max-size` 的文件跳过，避免 OOM。

术语解释见 [GLOSSARY.md](GLOSSARY.md)。
