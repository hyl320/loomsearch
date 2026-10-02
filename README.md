# LoomSearch

LoomSearch 是一个基于 C++20 线程池的并行文件索引器：递归扫描文件，并发读取和分词，构建倒排索引，然后从索引文件中查询关键词。

## 支持范围

第一版只索引这些文件：

```text
.txt
.md
.cpp
.hpp
```

暂不做 GUI、HTTP 服务、模糊搜索、增量索引和文件监听。

另外接入了 SQLite FTS5 作为第二个检索后端：在保留手写倒排索引的同时，用 FTS5 提供 BM25 排名、短语查询和前缀查询。

## 构建

```powershell
cmake -S . -B build -DLOOMLOG_BUILD_CLI=ON
cmake --build build
```

生成的命令行程序位于：

```text
build\loomsearch.exe
```

## 使用

建立索引：

```powershell
.\build\loomsearch.exe index .\documents --threads 8 --output .\index.bin
```

查询索引：

```powershell
.\build\loomsearch.exe query .\index.bin "thread pool"
```

输出示例：

```text
matches: 3
documents/thread_pool.txt
documents/logger.md
documents/design.md
```

多个关键词使用 AND 语义。例如 `thread pool` 只返回同时包含 `thread` 和 `pool` 的文件。

### SQLite FTS5 后端

除了手写倒排索引，还内置了一个基于 SQLite FTS5 的检索后端：

```powershell
.\build\loomsearch.exe sqlite-index .\documents --threads 8 --output .\index.db
.\build\loomsearch.exe sqlite-query .\index.db "thread pool"
```

FTS5 额外提供手写索引没有的能力：

- **BM25 排名**：结果按相关度从高到低排序。
- **短语查询**：`sqlite-query .\index.db '"thread pool"'` 只命中相邻词组。
- **前缀查询**：`sqlite-query .\index.db 'thread*'` 命中 `thread`、`threads` 等。

SQLite 以官方 amalgamation 形式 vendor 在 `third_party/sqlite/`，构建完全离线。

## 调试命令

旧命令仍然保留，用于观察线程池和日志组件：

```powershell
.\build\loomsearch.exe bench-pool --threads 8 --jobs 100000
.\build\loomsearch.exe bench-log --messages 100000 --file .\bench.log
.\build\loomsearch.exe grep --file .\loomsearch.log --keyword processed
```

## 架构

```text
手写后端:
  index:
    DirectoryScanner -> ThreadPool -> FileProcessor -> FileResult
                                        |
                                        v
                               InvertedIndex -> IndexStore

  query:
    IndexStore -> QueryEngine -> matches

SQLite FTS5 后端:
  sqlite-index:
    DirectoryScanner -> ThreadPool -> 读原文 -> SqliteStore (FTS5)

  sqlite-query:
    SqliteStore (FTS5 MATCH + BM25) -> matches
```

worker 线程只负责读取和解析文件，不直接修改共享索引。主线程统一合并 `FileResult`，减少锁竞争，也让并发边界更容易解释和测试。

## 验证

运行全部测试：

```powershell
ctest --test-dir build --output-on-failure
```

只跑 CLI 往返测试：

```powershell
.\build\test_cli.exe
```

手动端到端验证：

```powershell
New-Item -ItemType Directory -Force -Path .\out\loomsearch-demo
Set-Content -Path .\out\loomsearch-demo\thread_pool.txt -Value 'thread pool worker'
Set-Content -Path .\out\loomsearch-demo\logger.md -Value 'thread logger'
.\build\loomsearch.exe index .\out\loomsearch-demo --threads 2 --output .\out\loomsearch-demo\index.bin
.\build\loomsearch.exe query .\out\loomsearch-demo\index.bin "thread pool"
```

期望输出包含：

```text
matches: 1
thread_pool.txt
```

## Benchmark

本机轻量数据，时间为一次运行结果，用于观察量级，不作为跨机器结论：

```text
处理器标识: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel
线程池: 100000 个任务，4 线程，1759 ms
std::async: 100000 个任务，17988 ms
异步日志: 1000000 条消息，submit 1142 ms，total 1142 ms，dropped 0
```

搜索后端对比（`corpus-gen` 生成 2000 个合成文件、约 1.1 MB，4 线程，30 个查询，一次运行）：

```text
手写索引    build 271 ms, 索引 3.2 MB, query 总计 28.8 ms
SQLite FTS5 build 178 ms, 索引 2.1 MB, query 总计 165.2 ms
parity_mismatches: 0
```

生成可复现的 benchmark 语料并对比两个后端：

```powershell
.\build\loomsearch.exe corpus-gen .\out\corpus --files 2000 --words 80
.\build\loomsearch.exe bench-search .\out\corpus --threads 4 --queries 30
```

## 设计取舍

- worker 返回 `FileResult`，主线程合并索引，避免多个线程同时写 `unordered_map`。
- 索引格式先用文本格式，方便查看、调试和测试。
- 查询先支持单关键词和多关键词 AND，不引入排名系统。
- SQLite FTS5 后端与手写索引并存：FTS5 用紧凑 posting 编码 + BM25/短语/前缀，构建更快、磁盘更小，但单条查询比内存哈希倒排索引慢；手写索引胜在无依赖、可解释、查询快。
- FTS5 用 unicode61 分词，手写索引用 ASCII 分词，两者在 ASCII 单词上结果一致（benchmark 的 `parity_mismatches` 为 0）。
- Logger 使用独立后台线程，不放进文件处理线程池，避免监控日志被慢任务拖住。
