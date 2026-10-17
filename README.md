# Yat-CAShed

[![CI](https://github.com/mafuquan/Yat-CAShed/actions/workflows/ci.yml/badge.svg)](https://github.com/mafuquan/Yat-CAShed/actions/workflows/ci.yml)
[![License: GPL-2.0](https://img.shields.io/badge/kernel-GPL--2.0-blue.svg)](linux-6.8-main/COPYING)
[![Java](https://img.shields.io/badge/simulator-JDK%2017%2B-orange.svg)](simulator)
[![Kernel](https://img.shields.io/badge/kernel-Linux%206.8-informational.svg)](linux-6.8-main)

> **Yat-CAShed（Yet Another Task Cache-Aware Scheduler）**
> 一个面向多核 Linux 系统的缓存感知任务调度项目，涵盖内核调度器实现、调度算法模拟器与完整的论文/实验资料。

## 项目概述

Yat-CAShed 基于 AJLR（Allocation of parallel Jobs based on Learned cache Recency）思想，实现了一个内置于 Linux 内核的缓存感知调度类。其核心目标是：在**缓存亲和性**与**负载均衡**之间做动态权衡，减少任务跨核迁移导致的缓存失效，从而提升多核系统的整体性能。

- **调度策略 ID**：`SCHED_YAT_CASCHED = 8`
- **缓存感知机制**：维护任务历史表与加速表，记录 `last_cpu`、`per_cpu_recency` 等状态
- **核心算法**：基于 Cache Recency Profile（CRP）预测执行时间，结合 LCIF/MSF 策略做出（作业，核心）分配决策
- **工程实用性**：完整集成到 Linux 6.8 调度框架，可在 QEMU 中启动验证

## 文档导航

工程化文档统一收敛在 [`docs/`](docs) 目录，建议按以下顺序阅读：

| 文档 | 说明 |
|------|------|
| [`docs/README.md`](docs/README.md) | 文档总索引（入口） |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 系统架构与模块说明 |
| [`docs/DIRECTORY.md`](docs/DIRECTORY.md) | 目录结构详解 |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | 环境搭建与构建指南 |
| [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md) | ✅ 合并冲突分析与解决记录（已按路线 A 解决） |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | 改进与开发方向规划 |
| [`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md) | 模拟器实验报告（指标 / 统计 / 复现） |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | 贡献规范（分支 / 提交 / 代码风格） |

> ✅ **状态**：`kernel/sched/yat_casched.c` 的 **10 处 Git 合并冲突已按「路线 A（决赛 AJLR 版）」全部解决**，
> 当前文件无冲突标记、符号一致性检查通过。决策依据与执行明细见 [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md) §7–§8。

## 目录结构

```
Yat-CAShed/
├── README.md                    # 本文件
├── LICENSE                      # 许可证说明（分模块）
├── CONTRIBUTING.md              # 贡献规范
├── Makefile                     # 顶层编排（检查 / 模拟器 / 可视化）
├── docs/                        # 工程化文档体系（入口见 docs/README.md）
├── scripts/                     # 仓库运维脚本（check_repo.sh 等）
├── .github/workflows/           # CI（仓库健康检查 + 模拟器构建）
├── linux-6.8-main/              # Linux 6.8 内核源码 + Yat_Casched 调度器
│   ├── kernel/sched/yat_casched.c          # 调度器核心实现（AJLR，冲突已解决）
│   ├── include/linux/sched/yat_casched.h   # 调度器头文件
│   ├── Yat_Casched开发文档.md               # 完整实现文档（初赛版）
│   ├── yat_casched优化记录.md               # 迭代优化记录
│   ├── boot_test_scripts/                   # 用户态测试程序
│   ├── yat_test_scripts/                    # QEMU 测试环境脚本
│   └── test_visualization/                  # 性能测试与可视化
├── simulator/                   # 缓存感知调度算法模拟器（Java）
│   ├── Simulator.md                          # 模拟器详细文档
│   ├── AlgorithmComparisonExperiment.java    # 实验入口
│   ├── allocation/                           # WFD / CacheAware 算法实现
│   └── lib/                                  # 第三方依赖
└── report/                      # 论文、报告与参考资料
    ├── paper/                    # 毕业论文 LaTeX 源码
    ├── prompt_md/                # 分析笔记与计划
    └── *.pdf / *.tex             # 参考论文与讲稿
```

## 核心组件

### 1. 内核调度器（[`linux-6.8-main/`](linux-6.8-main)）

在 Linux 6.8 内核上新增 `Yat_Casched` 调度类，主要改动点：

| 文件 | 改动 |
|------|------|
| [`kernel/sched/yat_casched.c`](linux-6.8-main/kernel/sched/yat_casched.c) | 调度器核心（历史表、加速表、就绪池、AJLR 调度） |
| [`include/linux/sched/yat_casched.h`](linux-6.8-main/include/linux/sched/yat_casched.h) | 数据结构与接口声明 |
| `kernel/sched/core.c` | 优先级验证、调度类注册、初始化 |
| `kernel/sched/sched.h` | `yat_casched_policy()` 与 `rq` 扩展 |
| `kernel/sched/Makefile` | 编译接入 `yat_casched.o` |
| `init/Kconfig` | `SCHED_CLASS_YAT_CASCHED` 配置项 |
| `include/uapi/linux/sched.h` | `SCHED_YAT_CASCHED = 8` |
| `include/linux/sched.h` | `task_struct` 增加 `yat_casched` 实体 |
| `include/asm-generic/vmlinux.lds.h` | 链接器段注册 |

> 详细设计与实现说明见 [`Yat_Casched开发文档.md`](linux-6.8-main/Yat_Casched开发文档.md)，迭代记录见 [`yat_casched优化记录.md`](linux-6.8-main/yat_casched优化记录.md)。

### 2. 调度模拟器（[`simulator/`](simulator)）

基于 Java 的高性能调度算法模拟器，支持：

- 多算法对比：WFD（Worst Fit Decreasing）与 CacheAware（v1/v2）
- 多级缓存建模（L1/L2/L3）与缓存敏感度分析
- 可复现实验（固定随机种子）与批量验证
- 可视化结果（Makespan、缓存命中率、执行时间对比）

编译与运行（跨平台 GNU Make，Windows 亦可用 `build.bat`）：

```bash
# Windows
.\build.bat run            # 编译 + 实验 + 可视化

# Linux / macOS
make -C simulator test      # 单元测试（58 断言，零依赖）
make -C simulator verify    # 编译 + 依赖库自检 + 单元测试 + 快速实验
make -C simulator run-quick # 轻量可复现回放（5 用例 / 20 任务 / 种子 42）
make -C simulator run       # 全量实验（100 用例）+ 可视化
make -C simulator picture   # 单独生成可视化图表（需 Python + matplotlib）
```

可调参数（`AlgorithmComparisonExperiment`）：

| 参数 | 默认 | 说明 |
|------|------|------|
| `--cores=N` | 8 | 模拟核心数 |
| `--cases=N` | 100 | 实验用例总数（按利用率等级均匀分布） |
| `--tasks=N` | 60 | 每用例任务数 |
| `--seed=N` | 42 | 随机种子（保证可复现） |
| `--no-viz` | 关闭 | 跳过可视化，仅输出数值与 CSV |

### 可复现实验结果

**快速回放（`run-quick`，5 用例 / 20 任务 / 种子 42）**

| 指标 | 结果 |
|------|------|
| Makespan 平均改善 | **17.36%** |
| 缓存命中率平均改善 | **31.81 个百分点** |
| CacheAware_v2 胜率 | **100%** |

**全量实验（`make -C simulator run`，100 用例 / 60 任务 / 种子 42）**

| 指标 | 结果 |
|------|------|
| 平均 Makespan 改进 | **11.66%** |
| 平均缓存命中率改进 | **30.21 个百分点**（73.8% vs 43.6%） |
| CacheAware_v2 胜率 | **96 / 100 = 96.0%** |
| 覆盖率 | 利用率 0.6 / 0.8 / 1.0 / 1.2 / 1.5 各 20 例 |

> 数据由 `make -C simulator run` 生成：CSV 导出至 `simulator/result/algorithm_comparison_results.csv`，
> 3 张 300 dpi 图表（`makespan_analysis.png`、`cache_hit_analysis.png`、
> `execution_time_and_win_rate_analysis.png`）一并输出至 `simulator/result/`（该目录已被 `.gitignore` 忽略，属本地复现产物）。

完整说明见 [`Simulator.md`](simulator/Simulator.md)；**实验设计、完整统计与有效性讨论**见
[`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md)。

### 3. 论文与资料（[`report/`](report)）

- [`report/paper/`](report/paper)：毕业论文 LaTeX 源码（`Yat-CAShed.tex` 等章节文件）
- [`report/paper/Yat_Casched_完整实现文档.md`](report/paper/Yat_Casched_完整实现文档.md)：完整实现文档
- 参考论文：AJLR（RTNS'23）、Cache-Aware Allocation of Parallel Jobs on Multi-cores
- [`report/prompt_md/`](report/prompt_md)：分析与计划笔记

## 构建与测试（内核部分）

> 内核编译需在 Linux 环境（或 WSL）下进行，Windows 无法完整检出部分 Linux 专属文件名。

```bash
cd linux-6.8-main

# 启用 Yat_Casched 调度类
scripts/config --enable CONFIG_SCHED_CLASS_YAT_CASCHED

# 编译内核
make -j$(nproc)

# 使用 QEMU 快速验证
cd yat_test_scripts
./create_test_initramfs.sh
./start_test_environment.sh
```

用户态测试程序位于 [`boot_test_scripts/`](linux-6.8-main/boot_test_scripts)，性能对比与可视化见 [`test_visualization/`](linux-6.8-main/test_visualization)。

## 仓库健康检查

```bash
make check            # 或 bash scripts/check_repo.sh
bash scripts/check_symbols.sh   # 内核头文件/实现符号一致性（静态，容忍已知待决策项）
```

- `check_repo.sh`：冲突标记、空文件、编码、目录规范等守卫
- `check_symbols.sh`：核对 `yat_casched.h` 声明与 `yat_casched.c` 定义，暴露 `yat_global_pool_*` 等缺口
- 两者均已接入 CI（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）

## 主要依赖

- **内核**：GCC/Binutils、bison、flex、libssl-dev、libelf-dev、bc、dwarves、QEMU、busybox
- **模拟器**：JDK 17+（`openjdk-17-jdk-headless` 即可），第三方 JAR 位于 `simulator/lib/`（JGraphT、commons-math3、fastutil 等）
- **可视化**：Python 3 + matplotlib（必需）/ pandas、seaborn（可选，缺失时自动降级）；安装 `pip install -r simulator/visualizer/requirements.txt`

## 许可证

内核部分遵循 Linux 内核的 GPL-2.0 许可证（见 [`linux-6.8-main/COPYING`](linux-6.8-main/COPYING)）；模拟器与文档的学术使用权以各参考论文与项目说明为准。
