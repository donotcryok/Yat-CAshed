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
| [OPERATIONS.md](OPERATIONS.md) | **操作手册**：改完代码后怎么编译、怎么验证 A8、怎么读计数器、怎么发补丁（照着敲即可） |
| [DEPENDENCIES.md](DEPENDENCIES.md) | `simulator/lib/` 依赖审计报告（使用/未使用清单 + 建议） |
| [EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md) | 模拟器实验报告（WFD vs CacheAware_v2：设计、统计、图表、局限、复现） |
| [SCHEMA.md](SCHEMA.md) | 数据与产物格式约定：CSV 列定义 / schema 版本 / 图表命名 / 变更规则 |
| [DIRECTORY.md](DIRECTORY.md) | 目录结构与文件职责说明 |
| [archive/README.md](archive/README.md) | 归档目录说明与政策 |

> 阅读顺序建议：`PROJECT_AUDIT` → `ARCHITECTURE` → `DIRECTIONS` → `ROADMAP` → `DEVELOPMENT`。
> 若要**动手执行**（编译 / 跑 QEMU / 出数据 / 发补丁），直接看 [`OPERATIONS.md`](OPERATIONS.md)。

## 仓库级文档（项目根目录）

| 文件 | 说明 |
|------|------|
| [../README.md](../README.md) | 项目总览与快速开始 |
| [../CHANGELOG.md](../CHANGELOG.md) | 版本与变更记录 |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | 贡献规范（分支 / 提交 / 风格） |
| [../LICENSE](../LICENSE) | 分模块许可证说明 |

## 模块内既有文档

### 内核调度器（`linux-6.8-main/`）

- [`yat_casched优化记录.md`](../linux-6.8-main/yat_casched优化记录.md) — 迭代优化记录（变更日志）
- [`boot_test_scripts/README.md`](../linux-6.8-main/boot_test_scripts/README.md) — 用户态测试程序说明
- [`yat_test_scripts/README.md`](../linux-6.8-main/yat_test_scripts/README.md) — QEMU 测试环境脚本说明
- [`test_visualization/README.md`](../linux-6.8-main/test_visualization/README.md) — 性能测试与可视化说明

### 模拟器（`simulator/`）

- [`Simulator.md`](../simulator/Simulator.md) — 模拟器详细文档

### 论文与资料（`report/`）

- [`paper/`](../report/paper/) — 毕业论文 LaTeX 源码
- [`prompt_md/ajlr_paper_analysis.md`](../report/prompt_md/ajlr_paper_analysis.md) — AJLR 论文核心思想解析
- [`prompt_md/plan.md`](../report/prompt_md/plan.md) — 决赛版最小化实现方案

### 归档（`docs/archive/`）

- [`archive/README.md`](archive/README.md) — 归档政策与已归档清单
- [`archive/Yat-CAShed-初赛版实现文档.md`](archive/Yat-CAShed-初赛版实现文档.md) — 初赛版实现（历史）
- [`archive/Yat-CAShed-决赛版实现文档.md`](archive/Yat-CAShed-决赛版实现文档.md) — 决赛 AJLR 版实现（历史）

## 文档维护约定

1. **事实来源唯一**：调度类的**实际行为**以 `linux-6.8-main/kernel/sched/yat_casched.c`
   与 `include/linux/sched/yat_casched.h` 为准；架构层面见 [`ARCHITECTURE.md`](ARCHITECTURE.md)；
   变更历史见 [`../CHANGELOG.md`](../CHANGELOG.md)。两份「完整实现文档」是**历史快照**，
   已归档到 [`archive/`](archive/README.md)，不再作为事实来源（对应
   [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §7 的版本关系结论）。
2. **命名**（机械可判定，见 [`../CONTRIBUTING.md`](../CONTRIBUTING.md) §6）：
   - **项目名**：统一 `Yat-CAShed`。唯一例外是与外部实体绑定的不可变标识——
     GitHub 仓库路径（`github.com/donotcryok/Yat-CAShed`）、论文标题、`report/paper/Yat-CAShed.tex`
     等既有文件名，这些改动会破坏外部链接或构建，故保持原样。
   - **调度类名（散文）**：写作 `Yat-CAShed 调度类`，或直接引用注册名 `SCHED_YAT_CASCHED`。
   - **代码标识符**：一律 `yat_casched`（小写下划线）与 `YAT_CASCHED`（宏/策略名），
     由内核既有命名习惯决定，不随项目名调整。
   - `make name-check` 会对**散文层残留**做回归：白名单之外只要出现上表中的
     任一变体写法即失败，详见 [`scripts/check_names.py`](../scripts/check_names.py)。
3. **新增文档**：优先放入 `docs/`，并在本索引登记；模块专属文档留在模块目录。
4. **链接门禁**：`make docs-check` 会校验本索引及全仓库 Markdown 的相对链接，
   CI 已接入；新增/移动文档后请本地先跑一次。
5. **归档**：被取代的文档移入 `docs/archive/` 并在其 README 登记，不直接删除。
