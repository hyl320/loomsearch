# 已有手写倒排索引经验，未接触 SQLite 内部

用户已经在这个项目里实现了一个手写倒排索引（`InvertedIndex`：`keyword -> vector<path>`，ASCII 分词 + 多词 AND 交集查询），所以「倒排索引是什么、AND 查询怎么做」可以直接当作已掌握的基础，不必重新教。

SQLite 内部（B-tree、WAL、prepared statement、FTS5 实现）对他还是黑盒，是接下来要补的部分。据此，第一课应从「手写 posting list 与 FTS5 结构的一一对应」切入，而不是从零讲倒排索引。
