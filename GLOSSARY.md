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
| FileResult | worker 处理单个文件后返回的值对象：路径、词频字典、字节数、成功标志与错误信息。 |
| worker / 主线程合并 | 并发边界约定：worker 只读文件、分词并返回 `FileResult`，主线程统一合并共享索引，避免加锁。 |
| 增量索引（incremental index） | 只处理新增、修改、删除的文件，跳过未变化文件，避免每次全量重建；SQLite 后端基于 file state 实现。 |
| 原子持久化（atomic persistence） | 手写索引用影子临时文件刷盘后原子 `rename` 切换，防止进程中途退出损坏旧索引。 |
| magic header | 索引文件首行的固定标识（`LOOMSEARCH_INDEX_V1`），用于校验文件是否为合法索引。 |
| parity_mismatches | `bench-search` 输出：两个后端在同一查询集上返回结果集不一致的条数；应为 0。 |
| TF-IDF | 手写后端相关度评分，综合词频与平滑逆文档频率；得分越高越相关。 |
| QueryResult | 手写后端检索结果项，包含文档路径和 TF-IDF 分数。 |
| SqliteSearchHit | SQLite FTS5 命中结果，包含文档路径和 `bm25()` 分数。 |
| FileWatcher | 跨平台目录监听器：Windows 用 ReadDirectoryChangesW、Linux 用 inotify、macOS 用 FSEvents，文件变化时通过回调通知。 |
| 防抖（debounce） | 在时间窗口内压制同一文件的高频变化事件，等冷却结束后再批量处理。 |
| EventDebouncer | 文件事件防抖与状态机合并器，把同一文件连续变化合并成一次稳定事件。 |
| ContinuousIndexer | 常驻持续增量更新引擎，整合 FileWatcher、EventDebouncer、MemoryMappedFile 和 SqliteStore。 |
| MemoryMappedFile（mmap） | 跨平台只读内存映射封装，把文件内容以 `string_view` 交给分词器零拷贝处理，0 字节文件安全退化。 |
| WatchEvent | 文件监听事件，包含路径、类型（Created / Modified / Deleted）和时间戳。 |
| file state | SQLite 里保存的文件状态：路径、大小、修改时间、内容 hash，用于判断文件是否变化。 |
| schema_version / build_status | SQLite 元数据里的版本号和构建状态标记，启动时检测异常关机并自愈。 |
