# SQLite FTS5 面试速查卡

> 面试前 10 分钟过一遍。主线一句话 + 分模块结论 + 高频追问应对。

## 主线（脱稿一句话）

我手写的倒排索引是 `keyword -> 文件列表`，去重进了哈希表，没词频、没位置、没序词典，所以做不了排名/短语/前缀，也处理不了中文。FTS5 也是倒排索引，但 posting 里存了词频和位置、term 用排序 B-tree、磁盘用紧凑编码，所以能做 BM25/短语/前缀、磁盘还更小；默认 unicode61 分词能处理重音和 CJK。我用 SQLite C API 走「打开→准备→绑参→step→读列→释放」六步，配 WAL 让读写不互斥，几千次 insert 包进一个事务所以构建快；代价是查询走磁盘、比内存哈希慢。

## 手写倒排索引 vs FTS5

| 维度 | 手写倒排索引 | FTS5 |
|---|---|---|
| 结构 | `keyword -> vector<path>`（哈希表） | `term -> posting`（排序 B-tree） |
| posting 内容 | 只有路径 | 词频 + 位置 + docid |
| 排名 | 无 | BM25 |
| 短语 / 前缀 | 无 | 有（位置 + 序词典） |
| 磁盘 | 大（重复存完整路径） | 小（整数 docid + 变长编码） |
| 查询 | 快（内存哈希） | 慢（走磁盘 + 算分） |
| 分词 | ASCII（不认中文/重音） | unicode61（Unicode + 去重音） |

## BM25 三因子

- **tf（词频）**：越多越相关，但有饱和（`k1≈1.2` 控制）。
- **idf（逆文档频率）**：越稀有的词权重越高；常见词（如 `word0`）idf 低、区分度小。
- **文档长度归一化**：长文档打折（`b≈0.75` 控制）。

## C++ ↔ SQLite 六步

```
open(sqlite3_open_v2) -> prepare(sqlite3_prepare_v2) -> bind(sqlite3_bind_*) -> step(sqlite3_step) -> 读列(sqlite3_column_*) -> 释放(finalize / close)
```

- `?` 占位符 + bind = 防 SQL 注入（SQL 结构 和 值 分开传）。
- `SQLITE_ROW` = 有下一行可读；`SQLITE_DONE` = 取完。列用 `sqlite3_column_*` 按列号读。
- `SQLITE_TRANSIENT` = 让 SQLite 拷贝值，否则可能悬垂指针。
- RAII：用对象析构调 `finalize` / `close`，异常路径也安全。

## WAL

- 写先追加到 `-wal`，主库延迟到 checkpoint 才更新。
- 好处：读写不互斥（一个写者 + 多个读者）；写是顺序追加、更快。
- 三文件：`.db` / `-wal` / `-shm`。
- 代价：需共享内存（网络文件系统不可用）；wal 会临时变大。
- 构建快 = WAL 追加写 + 单事务批量写（避免每条 SQL 一次 fsync）。

## 短语 / 前缀

- 短语 `"thread pool"`：查是否存在 `pool` 的位置 = `thread` 的位置 + 1（需要位置列表）。
- 前缀 `word0*`：靠排序 term 词典（B-tree）按前缀找词；哈希表无顺序，只能扫全表。

## Tokenizer

- ASCII：字母数字算词、转小写、去重；非 ASCII 字节当分隔符 → 中文切不出词。
- unicode61：按 Unicode 分词 + 大小写折叠 + 去重音（`café`→`cafe`）；认得出 CJK，但不会真正中文分词。
- `parity_mismatches: 0` 是因为语料纯 ASCII；混入中文/重音就会分叉。

## benchmark（2000 文件 / 1.1MB / 4 线程 / 30 查询）

```text
hand  build 271ms, 3.2MB, query 28.8ms
FTS5  build 178ms, 2.1MB, query 165.2ms
parity_mismatches: 0
```

## 高频追问一句话应对

- 「为什么同时保留手写索引和 FTS5？」→ 一个无依赖、可解释、查询快，一个功能多、磁盘小；benchmark 才有对照（见 ADR-0001）。
- 「你的分词和 FTS5 差在哪？」→ ASCII vs unicode61；中文和重音就是分界线。
- 「WAL 有缺点吗？」→ 需共享内存、wal 临时变大、多进程要共享 wal。
- 「怎么防 SQL 注入？」→ prepared statement + `?` 参数绑定。
