# 厘清 sqlite3_step 的返回值语义与 SQLITE_TRANSIENT

用户起初把 `SQLITE_ROW` 理解成「返回的列」，实际含义是「游标前进到一行、有数据可读」；列要随后用 `sqlite3_column_*` 按列号读取。这是「游标 / 行 / 列」三层概念的关键区分，后续讲查询时要反复确认这一模型。

对 `SQLITE_TRANSIENT`（让 SQLite 拷贝绑定值）与「引用原指针、不拷贝」的区别用户也不清楚。要点：若让 SQLite 引用原指针而不拷贝，而 C++ 侧的 `string`/`string_view` 在语句执行前失效，就会悬垂指针（use-after-free），读到乱码或崩溃。这是 C++ 与 C 接口交接的常见坑，可作为未来「资源生命周期」主题固定下来。
