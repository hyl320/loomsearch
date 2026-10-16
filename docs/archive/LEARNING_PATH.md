# LoomSearch 学习路径

## 一、学习目标

完成这条路径后，应能够：

- 独立运行 LoomSearch CLI
- 讲清楚从扫描文件到查询结果的完整流程
- 理解线程池、`future`、条件变量和任务队列
- 理解倒排索引及多关键词 AND 查询
- 解释当前项目的设计取舍、性能瓶颈和改进方向
- 在面试中完整介绍项目并回答常见追问

## 二、阶段总览

| 阶段 | 主题 | 主要产出 |
|---|---|---|
| 1 | 跑通项目 | 能构建、建索引、查询 |
| 2 | 理解整体架构 | 能画出两条主流程 |
| 3 | 理解核心算法 | 能解释分词、倒排索引、交集查询 |
| 4 | 理解并发实现 | 能解释线程池和任务合并 |
| 5 | 理解存储和测试 | 能解释索引格式和测试方案 |
| 6 | 发现问题并改进 | 能提出可落地的升级方案 |
| 7 | 面试表达 | 能用 3 分钟和 10 分钟介绍项目 |

## 三、阶段 1：先跑通项目

### 1. 构建

```powershell
cmake -S . -B build -DLOOMLOG_BUILD_CLI=ON
cmake --build build
```

### 2. 查看帮助

```powershell
.\build\loomsearch.exe --help
```

### 3. 创建测试文件

```powershell
New-Item -ItemType Directory -Force -Path .\out\loomsearch-demo

Set-Content .\out\loomsearch-demo\thread_pool.txt 'thread pool worker'
Set-Content .\out\loomsearch-demo\logger.md 'thread logger'
```

### 4. 建立索引并查询

```powershell
.\build\loomsearch.exe index .\out\loomsearch-demo `
  --threads 2 `
  --output .\out\loomsearch-demo\index.bin

.\build\loomsearch.exe query `
  .\out\loomsearch-demo\index.bin `
  "thread pool"
```

### 阶段验收

能够说明：

- `index` 命令做什么
- `query` 命令做什么
- 为什么 `thread_pool.txt` 能匹配
- 为什么 `logger.md` 不能匹配

## 四、阶段 2：按主流程阅读源码

### 建立索引流程

按以下顺序阅读：

1. `src/cli.cpp`
2. `src/directory_scanner.cpp`
3. `src/indexer.cpp`
4. `src/file_processor.cpp`
5. `src/text_analyzer.cpp`
6. `src/inverted_index.cpp`
7. `src/index_store.cpp`

对应流程：

```text
main
  -> run_index
  -> DirectoryScanner::scan
  -> Indexer::build
  -> ThreadPool::submit
  -> FileProcessor::process
  -> tokenize_words
  -> InvertedIndex::add_document
  -> IndexStore::save
```

### 查询流程

重点阅读：

1. `src/cli.cpp`
2. `src/index_store.cpp`
3. `src/query_engine.cpp`
4. `src/text_analyzer.cpp`
5. `src/inverted_index.cpp`

对应流程：

```text
main
  -> run_query
  -> IndexStore::load
  -> normalize_query_terms
  -> InvertedIndex::find
  -> std::set_intersection
  -> 输出文件路径
```

### 阶段验收

手动画出两张图：

```text
文件 -> 分词 -> 关键词 -> 文件列表
```

```text
查询词 -> 查倒排索引 -> 多个文件列表求交集 -> 结果
```

## 五、阶段 3：理解核心数据结构和算法

### 1. 分词

当前规则：

- 只识别 ASCII 字母和数字
- 转换为小写
- 标点符号作为分隔符
- 单个文件内重复词会去重

例如：

```text
Thread, pool-worker!
```

转换为：

```text
thread
pool
worker
```

### 2. 倒排索引

核心结构：

```cpp
std::unordered_map<
    std::string,
    std::vector<std::string>
> index_;
```

含义：

```text
关键词 -> 包含该关键词的文件列表
```

示例：

```text
thread -> [logger.md, thread_pool.txt]
pool   -> [thread_pool.txt]
```

### 3. AND 查询

查询：

```text
thread pool
```

相当于：

```text
thread 对应的文件集合
与
pool 对应的文件集合
求交集
```

代码使用：

```cpp
std::set_intersection(...)
```

### 阶段验收

能够回答：

- 为什么使用倒排索引？
- `unordered_map` 和 `vector` 各自有什么作用？
- 为什么多关键词查询可以使用交集？
- 为什么索引建立时要对文件列表排序？

## 六、阶段 4：理解并发设计

重点阅读：

- `src/thread_pool.cpp`
- `include/loomlog/thread_pool.hpp`
- `src/indexer.cpp`

重点理解：

- 任务队列如何保存任务
- worker 线程如何等待任务
- 条件变量何时唤醒线程
- `future` 如何返回 `FileResult`
- `shutdown()` 如何停止线程池
- 任务异常如何传回主线程

本项目的重要设计是：

```text
worker 线程：
    只读取和解析文件
    返回独立的 FileResult

主线程：
    统一合并 FileResult
    修改共享的 InvertedIndex
```

这样避免多个 worker 同时写索引，降低锁竞争，也简化线程安全问题。

### 阶段验收

能够回答：

- 为什么不让 worker 直接写共享索引？
- 如果文件读取失败，程序如何处理？
- 如果任务抛出异常，主线程如何感知？
- 线程数设置过大有什么问题？
- CPU 密集型和 IO 密集型任务应该如何选择线程数？

## 七、阶段 5：理解索引存储和测试

阅读：

- `src/index_store.cpp`
- `tests/test_cli.cpp`
- `tests/test_indexer.cpp`
- `tests/test_query_engine.cpp`

当前索引格式包含：

```text
magic header
关键词数量
关键词
文档数量
文档路径
```

重点理解：

- 为什么需要 magic header
- 保存前为什么排序
- 加载索引时如何恢复 `InvertedIndex`
- CLI 往返测试如何验证“建立索引 + 查询”完整链路

运行测试：

```powershell
ctest --test-dir build --output-on-failure
```

## 八、阶段 6：分析限制并设计改进

当前项目的主要限制：

- 不支持中文分词
- 不支持模糊搜索
- 不支持 OR、NOT 和括号查询
- 不支持短语搜索
- 不支持增量索引
- 不支持文件变化监听
- 没有相关性排序
- 索引格式简单，压缩能力有限
- 单个文件会整体读入内存

推荐改进顺序：

### 第 1 个改进：完善错误处理和 CLI

- 增加明确的错误码
- 增加 `--help` 和参数校验
- 输出更稳定的机器可读格式

### 第 2 个改进：支持中文

- 先实现 UTF-8 解码
- 再实现中文单字索引
- 最后接入专业中文分词器

### 第 3 个改进：支持 OR 和 NOT

例如：

```text
thread OR logger
thread AND NOT pool
```

### 第 4 个改进：增量索引

- 保存文件修改时间和大小
- 只重新处理发生变化的文件
- 删除已不存在文件对应的索引记录

### 第 5 个改进：性能优化

- 使用分块读取
- 压缩倒排列表
- 使用更紧凑的文档 ID
- 减少字符串重复存储
- 设计磁盘上的分段索引

## 九、面试表达准备

### 3 分钟介绍模板

> 这是一个基于 C++20 的并行文本搜索工具。它递归扫描指定目录，使用线程池并行读取和分词，再由主线程统一构建倒排索引，将关键词映射到包含它的文件列表。索引保存到磁盘后，查询阶段只需要加载索引并对多个关键词对应的文件列表求交集，因此避免了每次查询重新扫描全部文件。项目中我重点处理了并发边界、索引持久化、错误处理和 CLI 测试。目前查询使用 AND 语义，后续可以扩展中文分词、增量索引和相关性排序。

### 高频追问

1. 倒排索引和数据库索引有什么相似之处？
2. 为什么使用线程池而不是每个文件创建一个线程？
3. 为什么 worker 不直接修改索引？
4. 当前查询的时间复杂度如何？
5. 如何支持中文？
6. 如何实现增量索引？
7. 如果索引文件损坏，如何恢复？
8. 如何测试并发代码？
9. 如何定位性能瓶颈？
10. 如果文件数量达到百万级，当前设计哪里会先出问题？

## 十、最终检查清单

- [ ] 能够从零构建并运行 CLI
- [ ] 能够解释 `index` 命令
- [ ] 能够解释 `query` 命令
- [ ] 能够画出建立索引流程
- [ ] 能够画出查询流程
- [ ] 能够解释倒排索引
- [ ] 能够解释线程池工作方式
- [ ] 能够解释为什么主线程合并结果
- [ ] 能够解释当前不支持中文的原因
- [ ] 能够指出至少三个性能问题
- [ ] 能够提出中文、增量索引和排序的实现方案
- [ ] 能够完成 3 分钟项目介绍
- [ ] 能够回答至少 10 个高频追问
