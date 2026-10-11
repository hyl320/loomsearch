# 领域文档

工程技能在探索代码库时，应该怎样消费本仓库的领域文档。

## 探索前先读这些

- 根目录的 **`GLOSSARY.md`**；或者
- 如果根目录存在 **`GLOSSARY-MAP.md`**：它指向每个上下文各自的 `GLOSSARY.md`，读与当前主题相关的那几份。
- **`docs/adr/`**：读涉及你即将工作区域的 ADR。多上下文仓库还要看 `src/<context>/docs/adr/` 里上下文级决策。

如果这些文件不存在，**静默继续**。不要提示它们的缺失，也不要提前建议创建。`/domain-modeling` 技能会在术语或决策真正确定时才懒创建它们。

## 文件结构

单上下文仓库（大多数仓库）：

```
/
├── GLOSSARY.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

多上下文仓库（根目录存在 `GLOSSARY-MAP.md`）：

```
/
├── GLOSSARY-MAP.md
├── docs/adr/                          ← 全系统级决策
└── src/
    ├── ordering/
    │   ├── GLOSSARY.md
    │   └── docs/adr/                  ← 上下文级决策
    └── billing/
        ├── GLOSSARY.md
        └── docs/adr/
```

## 使用术语表里的词

当你的输出要命名一个领域概念（issue 标题、重构方案、假设、测试名）时，使用 `GLOSSARY.md` 里定义的那个词，不要漂移到术语表明确避免的同义词。

如果你需要的概念还不在术语表里，那是个信号：要么你在发明项目不用的语言（重新考虑），要么确实有缺口（记下来交给 `/domain-modeling`）。

## 标记 ADR 冲突

如果你的输出和现有 ADR 矛盾，要显式指出，而不是默默覆盖：

> _与 ADR-0007（event-sourced orders）冲突，但值得重新讨论，因为……_
