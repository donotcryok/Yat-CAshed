# Yat-CAShed 文档中心

本目录集中管理项目的架构、开发、决策与方向性文档。原始的内核/模拟器/论文文档仍保留在各自模块目录内，此处提供统一索引与新增的工程化文档。

## 文档索引

| 文档 | 说明 |
|------|------|
| [PROJECT_AUDIT.md](PROJECT_AUDIT.md) | **工程化审计快照**：现状评估、问题清单、处理状态（建议先读） |
| [DIRECTIONS.md](DIRECTIONS.md) | **改进与开发方向建议**：候选方向、收益/成本、推荐路线 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 项目总体架构、模块组成与数据流 |
| [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) | `yat_casched.c` 合并冲突分析与解决记录（已按路线 A 解决） |
| [ROADMAP.md](ROADMAP.md) | 任务化改进清单（优先级 P0–P3 + 执行顺序） |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 开发环境、构建与测试指南 |
| [DEPENDENCIES.md](DEPENDENCIES.md) | `simulator/lib/` 依赖审计报告（使用/未使用清单 + 建议） |
| [EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md) | 模拟器实验报告（WFD vs CacheAware_v2：设计、统计、图表、局限、复现） |
| [DIRECTORY.md](DIRECTORY.md) | 目录结构与文件职责说明 |
| [archive/README.md](archive/README.md) | 归档目录说明与政策 |

> 阅读顺序建议：`PROJECT_AUDIT` → `ARCHITECTURE` → `DIRECTIONS` → `ROADMAP` → `DEVELOPMENT`。

## 仓库级文档（项目根目录）

| 文件 | 说明 |
|------|------|
| [../README.md](../README.md) | 项目总览与快速开始 |
| [../CHANGELOG.md](../CHANGELOG.md) | 版本与变更记录 |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | 贡献规范（分支 / 提交 / 风格） |
| [../LICENSE](../LICENSE) | 分模块许可证说明 |

## 模块内既有文档

### 内核调度器（`linux-6.8-main/`）

- [`Yat_Casched开发文档.md`](../linux-6.8-main/Yat_Casched开发文档.md) — 初赛版完整实现文档
- [`yat_casched优化记录.md`](../linux-6.8-main/yat_casched优化记录.md) — 迭代优化记录（变更日志）
- [`boot_test_scripts/README.md`](../linux-6.8-main/boot_test_scripts/README.md) — 用户态测试程序说明
- [`yat_test_scripts/README.md`](../linux-6.8-main/yat_test_scripts/README.md) — QEMU 测试环境脚本说明
- [`test_visualization/README.md`](../linux-6.8-main/test_visualization/README.md) — 性能测试与可视化说明

### 模拟器（`simulator/`）

- [`Simulator.md`](../simulator/Simulator.md) — 模拟器详细文档

### 论文与资料（`report/`）

- [`paper/`](../report/paper/) — 毕业论文 LaTeX 源码
- [`paper/Yat_Casched_完整实现文档.md`](../report/paper/Yat_Casched_完整实现文档.md) — 决赛版完整实现文档
- [`prompt_md/ajlr_paper_analysis.md`](../report/prompt_md/ajlr_paper_analysis.md) — AJLR 论文核心思想解析
- [`prompt_md/plan.md`](../report/prompt_md/plan.md) — 决赛版最小化实现方案

## 文档维护约定

1. **事实来源唯一**：`linux-6.8-main/Yat_Casched开发文档.md` 记录的是**初赛版**实现，
   `report/paper/Yat_Casched_完整实现文档.md` 记录的是**决赛 AJLR 版**实现。
   当前代码主线为决赛版（见 [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §7），
   涉及行为判断时**以代码与 [`ARCHITECTURE.md`](ARCHITECTURE.md) 为准**。
2. **命名**：仓库名/展示层用 `Yat-CAShed`；代码标识符统一 `yat_casched`。
   历史文档中的其他写法不做批量重写，以免破坏外部引用。
3. **新增文档**：优先放入 `docs/`，并在本索引登记；模块专属文档留在模块目录。
4. **链接门禁**：`make docs-check` 会校验本索引及全仓库 Markdown 的相对链接，
   CI 已接入；新增/移动文档后请本地先跑一次。
5. **归档**：被取代的文档移入 `docs/archive/` 并在其 README 登记，不直接删除。
