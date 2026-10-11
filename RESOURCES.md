# SQLite FTS5 学习资源

## Knowledge

- [SQLite 官方 FTS5 文档](https://www.sqlite.org/fts5.html)
  一手权威资料：FTS5 的建表语法、MATCH 查询语法、辅助函数（bm25、highlight、snippet）、tokenizer。讲任何 FTS5 细节前先查这里。
- [SQLite 官方 WAL 文档](https://www.sqlite.org/wal.html)
  解释 WAL 模式为什么让读写更友好，对应 benchmark 里 SQLite 构建阶段的落盘行为。
- [SQLite 官方 C 接口简介](https://www.sqlite.org/cintro.html)
  prepared statement 和参数绑定（`sqlite3_prepare_v2` / `sqlite3_bind_*`），对应项目里 `SqliteStore` 的实现。
- [Wikipedia: Okapi BM25](https://en.wikipedia.org/wiki/Okapi_BM25)
  BM25 的定义和公式，讲排名时引用，别凭记忆背公式。
- [Introduction to Information Retrieval（Stanford，免费在线）](https://nlp.stanford.edu/IR-book/)
  信息检索经典教材；倒排索引、posting list、排名（第 6、7 章）是权威来源。

## Wisdom (Communities)

- [r/sqlite](https://www.reddit.com/r/sqlite/)
  讨论 SQLite 用法和坑的高信号社区，适合验证 FTS5 的边角行为和排障经验。
