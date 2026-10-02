# LoomSearch 设计文档

## 项目定位

LoomSearch 是一个基于 C++20 线程池的并行文件索引器。它的重点不是展示线程池 API，而是让线程池承担真实的核心任务：

```text
扫描文件 -> 并发读取 -> 文本解析 -> 构建倒排索引 -> 查询
```

当前项目仍保留 `ThreadPool` 和异步 `Logger`，但主线从 `bench-pool`、`bench-log`、`grep` 升级为 `loomsearch index` 和 `loomsearch query`。

另外新增了 SQLite FTS5 检索后端，和手写倒排索引并存，用于对比「自研 vs 工业级」实现。

## 架构

```text
CLI
 ├── index          (手写倒排索引)
 │    ├── DirectoryScanner
 │    ├── ThreadPool
 │    ├── FileProcessor
 │    ├── InvertedIndex
 │    ├── IndexStore
 │    └── Logger
 │
 └── query
      ├── IndexStore
      └── QueryEngine

 ├── sqlite-index   (SQLite FTS5)
 │    ├── DirectoryScanner
 │    ├── ThreadPool
 │    ├── 读原文
 │    └── SqliteStore
 │
 └── sqlite-query
      └── SqliteStore
```

`index` 命令负责递归扫描目录，把每个支持的文件提交给线程池处理。worker 读取文件、分词并返回 `FileResult`。主线程拿到 future 后统一合并到 `InvertedIndex`，最后通过 `IndexStore` 保存到磁盘。

`query` 命令负责加载磁盘索引，把输入关键词归一化后交给 `QueryEngine` 查询。多个关键词使用 AND 语义，只有同时包含所有关键词的文档才会返回。

`sqlite-index` 复用同样的扫描和线程池，但 worker 只读原文、不做分词，主线程在一个事务里把全文插入 FTS5 表。`sqlite-query` 走 FTS5 的 `MATCH`，支持 BM25 排名、短语和前缀查询。

## 模块职责

- `ThreadPool`：执行文件读取和解析任务，提供固定数量 worker。
- `DirectoryScanner`：递归遍历目录，只返回 `.txt`、`.md`、`.cpp`、`.hpp` 文件。
- `FileProcessor`：读取文件内容，统计字节数，执行分词，返回 `FileResult`。
- `TextAnalyzer`：提供 ASCII 分词和查询词归一化，保证索引和查询使用同一套规则。
- `InvertedIndex`：维护 `keyword -> document list` 映射。
- `IndexStore`：保存和加载索引文件。
- `QueryEngine`：执行单关键词查询和多关键词 AND 查询。
- `SqliteStore`：封装 SQLite FTS5 的建表、批量入库和 `MATCH` 查询，`path` 只存不索引、`content` 参与检索。
- `Indexer`：编排扫描、线程池任务提交、结果收集、索引合并和统计。
- `Logger`：记录索引任务开始、文件处理、失败、完成和耗时。

## 核心数据结构

文件处理结果：

```cpp
struct FileResult {
    std::string path;
    std::unordered_set<std::string> words;
    std::size_t bytes{0};
    bool success{false};
    std::string error;
};
```

倒排索引：

```cpp
std::unordered_map<std::string, std::vector<std::string>>
```

第一版不保存词频、位置和排名分数。一个关键词只对应包含它的文件路径列表。

## 并发边界

LoomSearch 第一版刻意不让 worker 直接修改共享索引：

```text
worker 独立解析文件
  -> 返回 FileResult
  -> 主线程统一合并 InvertedIndex
```

这样倒排索引本身不需要锁，减少锁竞争，也更容易通过 TSan。线程池的职责停留在“并发执行 CPU/IO 混合任务”，索引合并保持单线程、确定性和容易测试。

## 分词规则

第一版采用简单 ASCII 分词：

- 字母和数字组成单词。
- 其他字符都是分隔符。
- 所有单词转小写。
- 重复词在单个文件内只保留一次。

例如：

```text
Thread_pool THREAD logger.md 42
```

会得到：

```text
thread, pool, logger, md, 42
```

## 索引格式

索引文件使用稳定的文本格式：

```text
LOOMSEARCH_INDEX_V1
<keyword-count>
<keyword>
<document-count>
<document-path>
...
```

保存时关键词和路径都会排序，保证文件内容稳定，方便调试和测试。第一版没有使用 SQLite、压缩或自定义二进制布局，因为当前目标是功能闭环和可解释性。

SQLite 后端用 FTS5 虚拟表存储：`CREATE VIRTUAL TABLE documents USING fts5(path UNINDEXED, content)`。FTS5 自己维护词频和位置，因此能提供手写索引没有的 BM25 排名、短语和前缀查询。FTS5 的 posting 编码比文本格式更紧凑，索引体积更小。

## 错误处理

目录不存在时，CLI 直接返回错误。

单个文件读取失败不会中断整个索引过程。`FileProcessor` 返回 `success=false` 和错误字符串，`Indexer` 统计失败数并继续处理其他文件。

索引文件无法保存、无法加载或 header 不合法时，`IndexStore` 抛出带上下文的异常，CLI 捕获后输出 `error: ...` 并返回非零退出码。

## Logger 角色

Logger 是任务监控组件，不是索引数据结构的一部分。索引时会记录：

- 任务开始。
- 扫描出的文件数量。
- 单文件处理成功。
- 单文件处理失败。
- 任务结束、成功数、失败数、字节数和耗时。

Logger 继续使用独立后台线程，不复用 `ThreadPool`。原因是日志刷盘需要稳定且尽量保序，不能被文件处理任务占满 worker 后饿死。

## 非目标

手写倒排索引这条路径不做（FTS5 后端已经覆盖其中一部分能力）：

- GUI。
- HTTP 服务。
- 模糊搜索。
- 增量索引。
- 文件监听。
- 分布式索引。
- 无锁队列。
- 压缩索引格式。

## 取舍

1. **为什么 worker 不直接写索引？**  
   共享 `unordered_map` 需要加锁，第一版没有必要。worker 返回独立结果，主线程合并，更容易测试和排查并发问题。

2. **为什么索引格式是文本？**  
   文本格式能直接查看、稳定断言，也避免二进制兼容细节。等功能和性能瓶颈明确后再升级格式。

3. **为什么查询只有 AND？**  
   AND 查询足以展示倒排索引的价值，也能保持结果解释简单。排名、OR、短语查询都需要额外数据结构，先不引入。

4. **为什么 Logger 不进线程池？**  
   线程池被文件处理任务占用时，日志仍需要稳定刷盘。独立 logger 线程能避免监控能力被业务任务拖住。

5. **为什么同时保留手写索引和 SQLite FTS5？**  
   两者回答不同问题：手写索引胜在无依赖、可解释、单条查询快（内存哈希查找）；FTS5 胜在构建更快、磁盘更小，并提供排名、短语和前缀查询。保留两者，benchmark 才能对比「自研轮子」和「工业级方案」的真实取舍，而不是把 SQLite 硬塞进一个没有动机的位置。

## 当前验证重点

- `FileProcessor`：普通文本、字节数、缺失文件。
- `DirectoryScanner`：递归扫描、扩展名过滤、大小写扩展名。
- `InvertedIndex`：关键词映射、路径去重、稳定快照。
- `IndexStore`：保存加载、坏 header、缺失索引文件。
- `QueryEngine`：大小写归一、单词查询、多词 AND、无命中。
- `Indexer`：并行构建、空目录、失败文件统计。
- `CLI`：真实 `index` + `query` 往返。

## Benchmark 记录

2026-09-16 在当前 Windows 开发环境运行了一组轻量 benchmark：

```text
处理器标识: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel

bench_pool:
  thread_pool task_count=100000 thread_count=4 elapsed_ms=1759
  std::async  task_count=100000 elapsed_ms=17988

bench_log:
  messages=1000000
  submit_ms=1142
  total_ms=1142
  dropped=0
```

这组数字只用于记录本机量级。后续如果要写进简历，应在固定机器、固定编译配置和固定任务规模下重复采样。

2026-10-02 在同一开发环境对比两个检索后端（`corpus-gen` 生成 2000 个合成文件、约 1.1 MB，4 线程，30 个查询）：

```text
hand_index_build_ms: 271
sqlite_index_build_ms: 178
hand_index_bytes: 3199272
sqlite_index_bytes: 2056192
hand_query_total_us: 28842
sqlite_query_total_us: 165206
parity_mismatches: 0
```

说明：手写后端的查询是内存热路径，SQLite 后端是磁盘上的 FTS5，所以单条查询前者快是预期结果；`parity_mismatches: 0` 说明两者在 ASCII 单词查询上返回相同结果集。

## 后续方向

后续只有在测试和 benchmark 给出明确需求后才扩展：

- 保存词频以支持排序。
- 保存文件更新时间以支持增量索引。
- 更紧凑的二进制格式。
- 更细的错误分类。
- 大目录 benchmark 和 TSan 常态化验证。
