# LoomSearch 设计文档

## 目标

LoomSearch 把当前项目从“线程池与日志组件示例”升级为一个基于线程池的并行文件索引器。项目主线变为：

```text
扫描文件 -> 并发读取 -> 文本解析 -> 构建倒排索引 -> 保存索引 -> 查询
```

第一版只处理本地文件系统，不做 GUI、网络服务、数据库、复杂排名、模糊搜索、增量索引或文件监听。

## 范围

必须实现：

- 递归扫描目录。
- 只索引 `.txt`、`.md`、`.cpp`、`.hpp` 文件。
- 使用现有 `loom::ThreadPool` 并行处理文件读取和分词。
- worker 返回独立 `FileResult`，主线程统一合并索引。
- 使用倒排索引结构保存 `keyword -> document list`。
- 保存索引到磁盘并从磁盘加载。
- 支持单关键词查询和多关键词 AND 查询。
- 统计扫描文件数、成功数、失败数、字节数、关键词数和耗时。
- 使用现有 `loom::Logger` 记录任务开始、完成、失败和耗时。
- 所有关键实现处写简洁中文注释，说明并发边界、数据流和错误处理意图。
- 更新中文 `DESIGN.md` 和中文 `README.md`。

暂不实现：

- GUI、HTTP 服务、数据库、压缩、BM25 排名、模糊搜索、增量索引、文件监听、分布式索引、无锁队列。

## 命令行体验

主命令：

```text
loomsearch index <directory> --threads <N> --output <index-file>
loomsearch query <index-file> <keywords...>
```

示例：

```text
loomsearch index .\documents --threads 8 --output .\index.bin
loomsearch query .\index.bin "thread pool"
```

查询输出固定为易读文本：

```text
matches: 3
documents/thread_pool.txt
documents/logger.md
documents/design.md
```

旧命令 `bench-pool`、`bench-log`、`grep` 保留，作为调试和性能观察入口，不再作为项目主线。

## 模块结构

新增和修改的主要模块：

```text
include/loomlog/file_result.hpp
include/loomlog/file_processor.hpp
include/loomlog/directory_scanner.hpp
include/loomlog/inverted_index.hpp
include/loomlog/indexer.hpp
include/loomlog/index_store.hpp
include/loomlog/query_engine.hpp

src/file_processor.cpp
src/directory_scanner.cpp
src/inverted_index.cpp
src/indexer.cpp
src/index_store.cpp
src/query_engine.cpp
src/cli.cpp
```

测试文件：

```text
tests/test_file_processor.cpp
tests/test_directory_scanner.cpp
tests/test_inverted_index.cpp
tests/test_index_store.cpp
tests/test_query_engine.cpp
tests/test_indexer.cpp
tests/test_cli.cpp
```

## 数据结构

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

第一版不保存词频和位置信息。每个关键词只记录包含它的文档路径，查询时按路径列表求交集。

## 分词规则

第一版采用确定性 ASCII 分词：

- 字母和数字属于单词。
- 其他字符作为分隔符。
- 单词统一转小写。
- 空字符串不进入结果。

这样 `.cpp` 和 `.hpp` 里的标识符会被下划线拆开，例如 `thread_pool` 会得到 `thread` 和 `pool`。这符合第一版的“能搜关键词”目标，也避免引入复杂解析器。

## 并发设计

worker 只负责处理单个文件：

```text
打开文件 -> 读取内容 -> 分词 -> 返回 FileResult
```

worker 不直接修改共享倒排索引。主线程按 future 完成结果顺序合并：

```text
for future in futures:
    result = future.get()
    index.add_document(result.path, result.words)
```

这个设计有三个收益：

- 倒排索引第一版不需要内部锁。
- TSan 更容易通过。
- 文件读取失败可以被主线程集中统计和记录。

## 错误处理

目录扫描失败时返回空结果并记录错误，CLI 把不可访问的根目录视为命令失败。

单个文件处理失败不会中断整次索引。`FileResult` 中 `success=false`，`error` 保存原因，`Indexer` 记录失败数并继续合并其他成功结果。

索引保存或加载失败会返回失败状态或抛出带上下文的异常，CLI 捕获后输出 `error: ...` 并返回非零退出码。

## 索引格式

第一版使用简单文本格式，避免大小端、结构体布局和二进制兼容问题：

```text
LOOMSEARCH_INDEX_V1
<keyword-count>
<keyword>
<document-count>
<document-path>
...
```

保存时关键词和路径按字典序排序，保证输出稳定，测试也更容易断言。读取时校验 magic header 和数量字段。

## Logger 使用

索引命令创建一个 `Logger`，日志文件默认为 `loomsearch.log`。记录内容包括：

- index started。
- discovered files。
- file processed。
- file failed。
- index finished。
- elapsed_ms、files_total、files_success、files_failed、bytes_total。

Logger 仍使用独立后台线程，不复用 ThreadPool。原因是日志刷盘需要尽量稳定且短，不应被文件处理任务占满 worker 后饿死。

## 测试策略

按 TDD 实施，每个模块先写失败测试，再写最小实现：

- `FileProcessor`：普通文本、大小写归一、字节数、缺失文件。
- `DirectoryScanner`：递归扫描、扩展名过滤、大小写扩展名。
- `InvertedIndex`：合并文档、去重、稳定快照。
- `IndexStore`：保存、加载、坏 header。
- `QueryEngine`：单关键词、多关键词 AND、大小写归一、无命中。
- `Indexer`：并发索引、空目录、单文件失败统计。
- `CLI`：index/query 端到端、旧调试命令仍可用。

最终验证：

- 普通 `ctest --test-dir build --output-on-failure`。
- 可用时运行 ASan + UBSan。
- 可用时运行 TSan。
- 构建 CLI 并执行一次真实 index/query 流程。

## 取舍记录

1. **为什么 worker 不直接写倒排索引？**  
   第一版优先保证并发边界清楚。worker 返回 `FileResult`，主线程合并，避免为 `unordered_map` 加锁，也减少 TSan 噪声。

2. **为什么索引格式先用文本？**  
   第一版目标是功能闭环和可测性。文本格式稳定、可查看、可断言，比二进制格式更适合早期开发。

3. **为什么不做排名？**  
   当前索引不保存词频和位置。先把扫描、并发处理、持久化和 AND 查询打通，再基于真实需求决定是否保存更多统计信息。

4. **为什么 Logger 不放入 ThreadPool？**  
   ThreadPool 是文件处理主力，可能被慢文件占满。Logger 独立线程能保持日志刷盘节奏，不被业务任务饿死。

## 完成标准

第一版完成时，用户可以在命令行运行：

```text
loomsearch index .\documents --threads 8 --output .\index.bin
loomsearch query .\index.bin "thread pool"
```

并得到稳定的匹配文件列表。所有新增模块有测试覆盖，中文 `README.md` 和 `DESIGN.md` 能解释项目定位、构建方式、架构、取舍和验证命令。
