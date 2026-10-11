# Mission：讲清 LoomSearch 的架构，以及为什么引入 SQLite FTS5

## Why

LoomSearch 用 C++20 线程池并行构建倒排索引，同时内置 SQLite FTS5 作为第二个检索后端，并和手写倒排索引做了 benchmark 对比。目标是在面试里把「整个项目的架构」「为什么引入 FTS5」「FTS5 内部怎么工作、和手写索引差在哪」讲清楚，而不是只会说「我用了 SQLite」。

## Success looks like

- 不看稿讲清 LoomSearch 从扫描、并发分词到倒排索引查询的完整链路，以及并发边界：worker 只解析、返回 `FileResult`，主线程统一合并。
- 讲清 FTS5 和手写倒排索引在结构上的一一对应（term → posting list、位置信息）。
- 解释 BM25 是什么、大致怎么算，为什么它比纯 AND 布尔匹配更能体现相关度。
- 解释短语查询和前缀查询为什么需要位置信息、倒排索引怎么支持它们。
- 讲清 benchmark 里「FTS5 构建更快、磁盘更小、单条查询更慢」的成因（posting 编码、磁盘 vs 内存、WAL/事务）。
- 接住常见追问：为什么不直接替换手写索引、FTS5 的 tokenizer 和 ASCII 分词差在哪、WAL 和 prepared statement 是什么。

## Constraints

- 目标是面试问答级，不深入 FTS5 的 C 源码实现。
- 用中文授课，一课一个主题、短小可完成。
- 每课尽量关联到 LoomSearch 里真实的代码和 benchmark 数字。

## Out of scope

- FTS5 源码级实现、写自定义 tokenizer 扩展。
- SQLite 的 B-tree 页格式、VDBE 字节码等底层存储细节。
- 通用 SQL / SQLite 全家桶（只覆盖和这个项目、这次面试相关的部分）。
