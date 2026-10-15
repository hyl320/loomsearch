# LoomSearch 查询语法与功能指南

LoomSearch 提供了两个检索后端：**手写内存倒排索引后端**与 **SQLite FTS5 工业级检索后端**。本篇文档详细说明各自的查询语法、高级过滤选项、排序保证以及错误提示机制。

---

## 1. 核心定位与后端分工

| 维度 | 手写后端 (`query`) | SQLite 后端 (`sqlite-query`) |
|---|---|---|
| **主打定位** | 极致轻量、内存热路径、可解释、教学与算法对照 | 功能完备、工业级全文检索、持久化与增量维护 |
| **查询语法** | 隐式多词 AND 布尔检索 | 丰富 MATCH 语法（AND / OR / NOT / 短语 / 前缀） |
| **相关度排序** | 自研经典 TF-IDF 算法（带平滑） | 工业级 BM25 算法（词频饱和 + 长度归一化） |
| **排序稳定性** | **绝对稳定**（TF-IDF 降序 + 路径字典序 Tie-breaker） | **稳定**（BM25 降序） |
| **过滤选项** | 支持 `--limit` 与 `--type` | 支持 `--limit` 与 `--type` |

---

## 2. 手写倒排索引后端 (`query`)

### 2.1 命令格式
```text
loomsearch query <index-file> <keywords...> [--limit N] [--type EXT]
```

### 2.2 查询与分词规则
1. **多词隐式 AND**：
   在命令行输入多个词条时，手写后端要求返回的文档必须**同时包含所有查询词**。
   例如：`loomsearch query index.bin thread pool` 仅返回同时包含 `thread` 和 `pool` 的文档。
2. **大小写与符号归一化**：
   查询词自动转为小写 ASCII，并剥除标点符号。例如输入 `Thread_Pool!` 将归一化为两个词条 `thread` 和 `pool`。
3. **TF-IDF 评分算法**：
   系统计算命中交集中每篇文档的综合得分：
   $$\text{score}(d) = \sum_{t \in terms} \Bigl(\text{tf}(t, d) \times \ln\Bigl(1.0 + \frac{N}{\text{df}(t)}\Bigr)\Bigr)$$
   - $\text{tf}(t, d)$：查询词 $t$ 在文档 $d$ 中出现的频次；
   - $N$：索引内总文档数量；
   - $\text{df}(t)$：包含查询词 $t$ 的文档总数；
   - 平滑项 $\ln(1.0 + N/\text{df})$ 保证了当所有文档均包含某词（$N=\text{df}$）时，得分不会退化为 0。
4. **排序稳定性保证**：
   - 主排序键：**TF-IDF 分数降序**（高相关度排在前面）；
   - 次排序键（Tie-breaker）：当两篇文档得分相差小于 $10^{-9}$ 时，按**文档路径字典序升序**排列，确保多次查询输出严格确定，消除哈希桶遍历顺序的不确定性。

---

## 3. SQLite FTS5 后端 (`sqlite-query`)

### 3.1 命令格式
```text
loomsearch sqlite-query <db-file> <query-expression...> [--limit N] [--type EXT]
```

### 3.2 高级检索语法（透传 FTS5 MATCH）
SQLite FTS5 后端原生支持完整的全文检索语法表达式：
1. **布尔运算**：
   - `AND`（隐式或显式）：`thread pool` 或 `thread AND pool`
   - `OR`：`thread OR pool`（只要命中任意一个词即返回）
   - `NOT`：`thread NOT logger`（包含 thread 且排除 logger）
2. **短语查询 (Phrase Query)**：
   使用双引号包裹，保证词条之间严格连续且位置相邻：
   `"thread pool"`
3. **前缀通配 (Prefix Query)**：
   支持星号 `*` 进行词条前缀匹配：
   `log*`（可匹配 logger, logging, logical 等）
4. **BM25 排名机制**：
   FTS5 内置 `bm25(documents)` 算法进行评分排序。相比单纯的 TF-IDF，BM25 引入了词频增长饱和度限制（高频重复词不会无节制膨胀得分）以及文档长度惩罚（长篇文档不会占便宜）。

---

## 4. 通用过滤选项

两个后端均支持以下过滤参数，参数位置任意（可放在关键词前、后或中间）：

### 4.1 数量限制 (`--limit N`)
限制输出命中文档的最大条数。$N$ 必须为正整数。
```powershell
# 仅返回相关度最高的前 5 条
loomsearch query index.bin thread --limit 5
loomsearch sqlite-query index.db "thread pool" --limit 5
```

### 4.2 文件类型过滤 (`--type EXT`)
按文档文件扩展名进行结果过滤，支持多种声明格式且大小写不敏感：
```powershell
# 单类型过滤（带点或不带点均可）
loomsearch query index.bin thread --type .cpp
loomsearch query index.bin thread --type cpp

# 逗号分隔的多类型过滤
loomsearch query index.bin thread --type .cpp,.hpp

# 多次指定 --type
loomsearch sqlite-query index.db thread --type .txt --type .md
```

---

## 5. 错误输入提示与校验

当命令行参数不合法时，CLI 会返回明确具体的错误说明（退出码非 0），并打印用法指南。常见错误场景如下：

| 错误场景 | 执行命令示例 | 错误信息输出 |
|---|---|---|
| **未提供关键词** | `loomsearch query index.bin` | `error: query requires a target file and keywords` |
| **索引文件不存在** | `loomsearch query missing.bin thread` | `error: index file does not exist: missing.bin` |
| **数据库文件不存在**| `loomsearch sqlite-query missing.db thread` | `error: database file does not exist: missing.db` |
| **--limit 未传参数**| `loomsearch query index.bin thread --limit` | `error: --limit requires a value` |
| **--limit 传非正整数**| `loomsearch query index.bin thread --limit 0` | `error: invalid value for --limit: 0` |
| **--type 未传参数** | `loomsearch query index.bin thread --type` | `error: --type requires a value` |
| **未知命令行选项** | `loomsearch query index.bin thread --unknown`| `error: unknown option for query: --unknown` |
