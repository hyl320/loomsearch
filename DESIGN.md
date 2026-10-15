# LoomSearch 设计文档

## 项目定位

LoomSearch 是一个基于 C++20 线程池的并行文件索引器。它的重点不是展示线程池 API，而是让线程池承担真实的核心任务：

```text
扫描文件 -> 并发读取 -> 文本解析 -> 构建倒排索引 -> 查询
```

主线命令是 `loomsearch index`、`loomsearch query`，以及 SQLite FTS5 后端的 `sqlite-index`、`sqlite-query`。`ThreadPool` 和异步 `Logger` 是支撑索引链路的基础组件。SQLite FTS5 后端与手写倒排索引并存，用于对比「自研 vs 工业级」实现。

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

`query` 命令负责加载磁盘索引，把输入关键词归一化后交给 `QueryEngine` 查询。多个关键词使用 AND 语义，计算每个候选文档的 TF-IDF 综合相关度得分，并按得分降序、路径字典序升序进行稳定输出，支持通过 `--limit` 和 `--type` 过滤。

`sqlite-index` 复用同样的扫描和线程池，但 worker 只读原文、不做分词，主线程在一个事务里把全文插入 FTS5 表。`sqlite-query` 走 FTS5 的 `MATCH`，支持 BM25 排名、短语、前缀查询以及 `--limit` 和 `--type` 过滤。

## 模块职责

- `ThreadPool`：执行文件读取和解析任务，提供固定数量 worker。
- `DirectoryScanner`：递归遍历目录，只返回 `.txt`、`.md`、`.cpp`、`.hpp` 文件。
- `FileProcessor`：读取文件内容，统计字节数，提取词频字典，返回 `FileResult`。
- `TextAnalyzer`：提供 ASCII 分词、词频统计和查询词归一化，保证索引和查询规则一致。
- `InvertedIndex`：维护 `keyword -> posting(doc list, tf map)` 倒排映射。
- `IndexStore`：保存和加载索引文件。
- `QueryEngine`：执行单/多关键词 AND 检索，计算 TF-IDF 评分并执行稳定排序。
- `SqliteStore`：封装 SQLite FTS5 的建表、批量入库和 `MATCH` 查询，`path` 只存不索引、`content` 参与检索。
- `Indexer`：编排扫描、线程池任务提交、结果收集、索引合并和统计。
- `Logger`：记录索引任务开始、文件处理、失败、完成和耗时。

## 核心数据结构

文件处理结果：

```cpp
struct FileResult {
    std::string path;
    std::unordered_map<std::string, std::size_t> words;  // word -> 词频 (TF)
    std::size_t bytes{0};
    bool success{false};
    std::string error;
};
```

倒排索引：

```cpp
struct PostingList {
    std::vector<std::string> docs;                    // 文档路径列表
    std::unordered_map<std::string, std::size_t> tf;  // path -> 词频 (TF)
};
std::unordered_map<std::string, PostingList> index_;
```

系统在构建期记录每个文档内关键词出现的频次（TF），查询期结合总文档数 $N$ 与文档频率 $\text{df}$ 计算 TF-IDF 评分。排序算法以相关度为第一关键字、路径字典序为第二关键字，保证结果稳定确定。

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

2026-10-02 在当前开发环境对比两个检索后端（`corpus-gen` 生成 2000 个合成文件、约 1.1 MB，4 线程，30 个查询）：

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

## 手写后端 vs SQLite FTS5 后端差异对照

本项目刻意实现了两个检索后端，用于在工程实践中对比「自研基础构件」与「工业级现成方案」的取舍：

### 1. 功能特性与能力矩阵

| 功能维度 | 手写倒排索引后端 (`loomsearch query`) | SQLite FTS5 后端 (`loomsearch sqlite-query`) |
|---|---|---|
| **核心定位** | 极致轻量、内存热路径、逻辑完全透明可控、教学与算法验证 | 功能完备、工业级生产可用、强事务与持久化保证 |
| **查询模型** | 纯布尔交集（隐式多词 AND） | 丰富 MATCH 语法（AND / OR / NOT / 短语 / 前缀） |
| **分词实现** | 手写 ASCII 字母数字分词（简单、无外部依赖） | 内置 Unicode61 分词器（支持多语言及音调处理） |
| **位置信息** | 不保存（仅记录文档词频 TF） | 默认记录精确 Token 偏移与位置（支持短语相邻匹配） |
| **排序机制** | **TF-IDF**（结合词频与标准平滑逆文档频率） | **BM25**（工业级标准：包含词频饱和与文档长度归一化） |
| **排序稳定性**| **绝对稳定**（得分降序 + 路径字典序 Tie-breaker） | **稳定**（BM25 降序） |
| **过滤选项** | `--limit N` 数量限制、`--type EXT` 后缀过滤 | `--limit N` 数量限制、`--type EXT` 后缀过滤 |
| **更新能力** | 全量重建（修改文件需重新执行全量索引） | 增量维护（基于 `file_states` 元数据表与时间戳感知） |
| **并发边界** | worker 分词返回 `FileResult`，主线程统一合并 | worker 仅读原文，主线程单事务批量入库 |

### 2. 排序算法差异：TF-IDF vs BM25

- **手写后端（平滑 TF-IDF）**：
  $$\text{score}(d) = \sum_{t} \text{tf}(t, d) \times \ln\Bigl(1.0 + \frac{N}{\text{df}(t)}\Bigr)$$
  特点是逻辑直观线性，词频越高权重线性增加；平滑项避免了全集词 IDF 退化为 0。
- **SQLite 后端（BM25）**：
  在 TF-IDF 基础上解决了两大工业缺陷：
  1. **词频饱和度（$k_1$ 参数）**：当某个词在同一文档中出现 100 次时，其相关度并不应该是出现 1 次的 100 倍。BM25 将 TF 限制在渐进渐平的饱和曲线上。
  2. **文档长度归一化（$b$ 参数）**：大文档包含更多词是天然的，BM25 根据文档实际长度与语料平均长度的比值进行惩罚，防止大文件天然占便宜。

### 3. 性能特征与 Benchmark 成因分析

在 2000 个文档合成语料的基准测试中：
- **建库耗时**：SQLite FTS5 略快于手写文本索引构建。原因是主线程通过显式 `BEGIN TRANSACTION` 将多文件插入合并为单次 WAL 顺序写入，且内部使用高效的变长字节压缩编码；
- **磁盘占用**：SQLite 数据库体积明显小于手写文本 `.bin` 文件。因为手写后端使用直观换行的可读文本格式存储路径与词条，而 FTS5 内部采用了前缀压缩与 Delta 编码的 B-Tree 页；
- **查询延时**：手写后端在内存热路径上的单词与多词查询（微秒级）显著快于 SQLite。因为手写后端全部数据在 `InvertedIndex` 哈希表与 `vector` 中，无需经过 SQL 解析、虚拟表回调以及 SQLite VDBE 字节码引擎调度。

## 后续方向

后续只有在测试和 benchmark 给出明确需求后才扩展：

- 增量手写索引（引入元数据状态与差异比对）。
- 更紧凑的手写二进制或压缩索引格式。
- 大目录 benchmark 和 TSan 常态化验证。
