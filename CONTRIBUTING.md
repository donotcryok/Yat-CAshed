# 贡献指南（Contributing Guide）

感谢参与 Yat-Casched。本文件约定分支模型、提交规范与检查流程。

## 1. 分支模型

| 分支 | 用途 |
|------|------|
| `main` | 稳定主线，始终应可构建、无冲突标记 |
| `yat/yat_casched-dev` | 决赛 AJLR 版开发分支（历史遗留，仍存在于远端） |
| `feat/<topic>` | 新特性 |
| `fix/<topic>` | 缺陷修复 |
| `docs/<topic>` | 文档 |

> ✅ `main` 上的 `kernel/sched/yat_casched.c` 曾残留 `main` 与 `yat/yat_casched-dev` 的合并冲突，
> **已按「路线 A（决赛 AJLR 版）」解决**，仓库健康检查与 CI 现对任何冲突标记一律阻断。
> 决策依据与执行明细见 [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md)。

## 2. 提交信息规范

采用约定式提交（沿用仓库现有风格）：

```
<type>(<scope>): <subject>

[optional body]
[optional footer]
```

`type` 取值：`feat` `fix` `docs` `chore` `refactor` `test` `perf` `build` `ci`。

示例：

```
fix(kernel): 移除 yat_casched.c 中的合并冲突标记
docs(docs): 新增 CONFLICT_ANALYSIS 与 ROADMAP
feat(simulator): 支持从配置文件读取实验参数
```

`scope` 建议取值：`kernel` / `simulator` / `report` / `docs` / `scripts`。

## 3. 提交前检查

最小门禁（快速）：

```bash
make check        # 冲突标记 / 空文件 / 关键文件检查
make docs-check   # Markdown 相对链接有效性
```

涉及模拟器的改动，请额外确认可编译并通过单测：

```bash
make simulator-verify   # 编译 + 依赖自检 + 58 条断言 + 快速实验
# Windows 下可用：cd simulator && build.bat compile && build.bat run
```

涉及内核的改动，建议先单文件编译以获得快速反馈：

```bash
cd linux-6.8-main && make kernel/sched/yat_casched.o
make -C .. symbol-check   # 头文件声明 vs 实现定义
```

全量门禁（提交 PR 前推荐）：

```bash
make verify-all   # check + docs-check + symbol-check + simulator-verify
```

## 4. 代码风格

- **内核代码**：遵循 Linux 内核编码规范（Tab 缩进、`checkpatch.pl`）。
  提交前运行 `linux-6.8-main/scripts/checkpatch.pl --no-tree -f <file>`。
- **Java**：4 空格缩进，类名 `PascalCase`，方法/变量 `camelCase`，包名保持现有小写目录风格。
- **Python**：PEP 8，4 空格缩进。
- **Markdown**：标题层级连续，表格对齐；文档统一放 `docs/`，模块内文档沿用各模块目录。
- 通用格式约定见 [`.editorconfig`](.editorconfig)。

## 5. 文档同步要求

任何影响以下内容的改动，请同步更新对应文档：

| 改动 | 需更新 |
|------|--------|
| 目录/模块结构 | `docs/ARCHITECTURE.md`、`docs/DIRECTORY.md` |
| 构建/依赖 | `docs/DEVELOPMENT.md`、相关 `Makefile` |
| 算法/接口 | 模块内 README、`docs/ARCHITECTURE.md` |
| 方向取舍 | `docs/ROADMAP.md`、`docs/DIRECTIONS.md` |
| 实验数据/结论 | `docs/EXPERIMENT_REPORT.md` |
| 调度器行为变化 | `linux-6.8-main/yat_casched优化记录.md`、`CHANGELOG.md` |

## 6. 命名约定

- 项目名统一：**Yat-Casched**（显示层可用 `Yat-CAShed`）。
- 代码标识符统一：`yat_casched`（小写下划线）。
- 避免在同一文档中混用 `Yat_Casched` / `Yat_CASched` / `YAT_CASCHED`。
