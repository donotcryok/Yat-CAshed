# Yat-CAShed 文档中心

本目录集中管理项目的架构、开发、决策与方向性文档。原始的内核/模拟器/论文文档仍保留在各自模块目录内，此处提供统一索引与新增的工程化文档。

## 文档索引

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 项目总体架构、模块组成与数据流 |
| [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) | ✅ `yat_casched.c` 合并冲突分析与解决记录（**已按路线 A 解决**） |
| [ROADMAP.md](ROADMAP.md) | 改进与开发方向（工程 / 内核 / 模拟器 / 论文） |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 开发环境、构建与测试指南 |
| [DEPENDENCIES.md](DEPENDENCIES.md) | 📦 `simulator/lib/` 依赖审计报告（使用/未使用清单 + 建议） |
| [EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md) | 📊 模拟器实验报告（WFD vs CacheAware_v2：设计、统计、图表、局限、复现） |
| [DIRECTORY.md](DIRECTORY.md) | 目录结构与文件职责说明 |
| [archive/README.md](archive/README.md) | 归档目录说明与政策 |

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

## 已知文档问题（待整理）

1. **实现文档重复且分叉**：`linux-6.8-main/Yat_Casched开发文档.md`（初赛版，26KB）与 `report/paper/Yat_Casched_完整实现文档.md`（决赛版，39KB）内容不同、各自演进，缺少“以谁为准”的说明。
2. **命名不统一**：项目名存在 `Yat-CAShed` / `Yat_Casched` / `Yat_CASched` / `YAT_CASCHED` 等多种写法。
3. 中文文档中夹杂大量英文命令行示例，且部分 README 使用了表情符号与不一致的标题层级。

> 上述条目记录在案，具体重构建议见 [ROADMAP.md](ROADMAP.md)。
