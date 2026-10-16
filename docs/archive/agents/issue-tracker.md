# Issue tracker：GitHub

本仓库的 issue 和规格说明都放在 GitHub Issues 里，所有操作统一用 `gh` CLI。

## 约定

- **创建 issue**：`gh issue create --title "..." --body "..."`。多行正文用 heredoc。
- **读取 issue**：`gh issue view <number> --comments`，用 `jq` 过滤评论，同时拉取标签。
- **列出 issue**：`gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'`，并按需加 `--label`、`--state` 过滤。
- **评论 issue**：`gh issue comment <number> --body "..."`
- **打标签 / 去标签**：`gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **关闭**：`gh issue close <number> --comment "..."`

仓库从 `git remote -v` 推断；在 clone 出的目录里 `gh` 会自动识别。

## Pull requests as a triage surface

**PRs as a request surface: no.**（如果本仓库把外部 PR 当作功能请求，就改成 `yes`；`/triage` 会读取这个标志。）

设为 `yes` 时，PR 走和 issue 一样的标签与状态机，只是命令换成 `gh pr` 系列：

- **读取 PR**：`gh pr view <number> --comments`，diff 用 `gh pr diff <number>`。
- **列出需要 triage 的外部 PR**：`gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments`，只保留 `authorAssociation` 为 `CONTRIBUTOR`、`FIRST_TIME_CONTRIBUTOR` 或 `NONE` 的（去掉 `OWNER`/`MEMBER`/`COLLABORATOR`）。
- **评论 / 打标签 / 关闭**：`gh pr comment`、`gh pr edit --add-label`/`--remove-label`、`gh pr close`。

GitHub 的 issue 和 PR 共用一个编号空间，所以光秃秃的 `#42` 可能指两者之一：先用 `gh pr view 42` 试，失败再回退到 `gh issue view 42`。

## 当技能说「publish to the issue tracker」

创建一个 GitHub issue。

## 当技能说「fetch the relevant ticket」

执行 `gh issue view <number> --comments`。

## Wayfinding 操作

供 `/wayfinder` 使用。**map** 是一个 issue，其 **child** issue 作为工单。

- **Map**：一个打了 `wayfinder:map` 标签的 issue，正文放 Notes / Decisions-so-far / Fog。命令：`gh issue create --label wayfinder:map`。
- **Child ticket**：通过 GitHub sub-issue 端点（`gh api`）关联到 map 的 issue。如果 sub-issue 未启用，就把 child 加进 map 正文的任务清单，并在 child 正文顶部写 `Part of #<map>`。标签：`wayfinder:<type>`（`research`/`prototype`/`grilling`/`task`）。被认领后，把工单分配给负责开发的 agent。
- **Blocking（阻塞）**：用 GitHub **原生 issue dependencies**，这是 UI 可见的规范表示。加一条边：`gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`，其中 `<blocker-db-id>` 是阻塞项的数值 **database id**（用 `gh api repos/<owner>/<repo>/issues/<n> --jq .id` 取，注意不是 `#number` 也不是 `node_id`）。GitHub 通过 `issue_dependencies_summary.blocked_by` 报告（只含未关闭的阻塞项，即「活的」门）。如果 dependencies 不可用，就在 child 正文顶部写一行 `Blocked by: #<n>, #<n>`。当所有阻塞项都关闭时，工单才算解除阻塞。
- **Frontier 查询**：列出 map 的未关闭 child（`gh issue list --state open`，范围限定到 map 的 sub-issue / 任务清单），去掉有未关闭阻塞项（`issue_dependencies_summary.blocked_by > 0`，或 `Blocked by` 行里有未关闭 issue）或已有 assignee 的；按 map 顺序取第一个。
- **Claim（认领）**：`gh issue edit <n> --add-assignee @me`，这是会话里的第一次写操作。
- **Resolve（解决）**：`gh issue comment <n> --body "<answer>"`，然后 `gh issue close <n>`，再把一个上下文指针（gist + 链接）追加到 map 的 Decisions-so-far。
