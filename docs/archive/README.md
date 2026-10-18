# 归档区（Archive）

本目录用于存放**已被取代但仍需保留**的历史文档。归档文件不再作为“当前事实来源”。

## 归档政策

1. 只在文档**确实被取代**后归档，并在下表登记；
2. 归档时保留原文件名，必要时在文件顶部加一行状态说明；
3. 归档文件**仍参与** `make docs-check` 的链接校验，避免链接腐烂；
4. 不直接删除历史文档——仓库体积敏感的内容（大 PDF/PPT）另行处理。

## 当前状态

已归档 2 份历史实现文档（P1 文档治理，见 [`../../CHANGELOG.md`](../../CHANGELOG.md)）：

| 归档文件 | 原位置 | 定位 |
|----------|--------|------|
| [`Yat-CAShed-初赛版实现文档.md`](Yat-CAShed-初赛版实现文档.md) | `linux-6.8-main/Yat_Casched开发文档.md` | 初赛版的实现描述 |
| [`Yat-CAShed-决赛版实现文档.md`](Yat-CAShed-决赛版实现文档.md) | `report/paper/Yat_Casched_完整实现文档.md` | 决赛 AJLR 版的实现描述 |

归档理由：

1. 两份文档既不是内核源码（不参与编译），也不是论文源（不参与 `make report`），
   却分别散落在 `linux-6.8-main/` 与 `report/paper/` 里，使「改代码」与「读旧文档」
   的人都要跨三个顶层目录找材料。
2. 两份内容高度重合（同为「完整实现文档」），留在原位会持续制造
   「哪一份是事实来源」的歧义 —— 参见 [`../ARCHITECTURE.md`](../ARCHITECTURE.md) §5.1。
3. 归档**不是删除**：文件仍参与 `make docs-check` 的链接校验，仍可全文检索，
   git 历史亦完整保留（`git log --follow` 可用）。

> **当前事实来源（single source of truth）**：
> 调度类的**实际行为**一律以 `linux-6.8-main/kernel/sched/yat_casched.c` 与
> `include/linux/sched/yat_casched.h` 为准；架构层面的说明见
> [`../ARCHITECTURE.md`](../ARCHITECTURE.md)；变更历史见 `CHANGELOG.md`。
> 两份归档文档反映的是**当时的**实现，个别细节（如 `NR_CPUS` 规模的
> `history_tables` 成员、未使用的 `per_cpu_last_ts`）已在后续 A5 轮次中被删除，
> 归档正文未随之改动 —— 这是归档文档应有的形态，刻意保持原样。

## 已记录的历史版本关系

当前代码主线为**决赛 AJLR 版**（见 [`../CONFLICT_ANALYSIS.md`](../CONFLICT_ANALYSIS.md) §7）。
两份实现文档已收敛到本目录，定位如下：

| 文档 | 定位 | 状态 |
|------|------|------|
| [`Yat-CAShed-初赛版实现文档.md`](Yat-CAShed-初赛版实现文档.md) | 初赛版实现描述 | 历史版本，仅作参考 |
| [`Yat-CAShed-决赛版实现文档.md`](Yat-CAShed-决赛版实现文档.md) | 决赛版实现描述 | 历史版本，主线思路的来源 |

## 后续可考虑归档的对象

| 对象 | 原因 | 前置条件 |
|------|------|----------|
| `report/初赛项目报告(1)(1).txt` | 早期草稿，已被论文取代 | 论文定稿 |
| `report/proj59.pptx` / `proj59.pdf` | 体积大（≈24MB / ≈4.5MB） | 改用 Release 附件或 Git LFS |
| `report/paper/yat_CAsched.pdf` | 历史编译产物 | 确认 `make report` 可复现后 |

> 目前这些文件仍保留在原位置，仅在此登记，不做实际移动。
