# GLOSSARY

LoomSearch 领域术语表。命名 issue、重构方案、测试名或面试表达时优先使用这里的词。

| 术语 | 定义 |
| --- | --- |
| LoomSearch | 项目/产品名。基于 C++20 线程池的并行文件索引器，内置手写倒排索引与 SQLite FTS5 两个检索后端。 |
| 倒排索引（inverted index） | 从「关键词 → 包含它的文档列表」的映射，用于快速回答「哪些文档包含这个词」。 |
| posting list | 倒排索引中某个关键词对应的文档 ID/路径列表。FTS5 用紧凑编码存储。 |
| 词频（term frequency） | 一个词在单个文档里出现的次数；BM25 的核心输入之一。 |
| BM25 | Okapi BM25 排名函数，根据词频、文档长度和逆文档频率给「文档-查询」打分排序。 |
| FTS5 | SQLite 的全文检索虚拟表模块，内置 BM25 排名、短语查询和前缀查询。 |
| tokenizer / 分词器 | 把文本切成词项的组件。LoomSearch 手写后端用 ASCII 分词，FTS5 用 unicode61。 |
| unicode61 | FTS5 默认 tokenizer，按 Unicode 字母/数字切词；与 LoomSearch 的 ASCII 分词在 ASCII 单词上结果一致。 |
| WAL（Write-Ahead Log） | SQLite 的一种日志模式：写入先落 WAL，读不阻塞写、写不阻塞读。 |
| prepared statement | SQLite 的预编译语句（`sqlite3_prepare_v2` + `sqlite3_bind_*`），用于安全、高效地重复执行同一 SQL。 |
| FileResult | worker 处理单个文件后返回的值对象：路径、词集合、字节数、成功标志与错误信息。 |
| worker / 主线程合并 | 并发边界约定：worker 只读文件、分词并返回 `FileResult`，主线程统一合并共享索引，避免加锁。 |
| magic header | 索引文件首行的固定标识（`LOOMSEARCH_INDEX_V1`），用于校验文件是否为合法索引。 |
| parity_mismatches | `bench-search` 输出：两个后端在同一查询集上返回结果集不一致的条数；应为 0。 |
