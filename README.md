# Yat-CAShed

> **Yat-CAShed（Yet Another Task Cache-Aware Scheduler）**
> 一个面向多核 Linux 系统的缓存感知任务调度项目，涵盖内核调度器实现、调度算法模拟器与完整的论文/实验资料。

## 项目概述

Yat-CAShed 基于 AJLR（Allocation of parallel Jobs based on Learned cache Recency）思想，实现了一个内置于 Linux 内核的缓存感知调度类。其核心目标是：在**缓存亲和性**与**负载均衡**之间做动态权衡，减少任务跨核迁移导致的缓存失效，从而提升多核系统的整体性能。

- **调度策略 ID**：`SCHED_YAT_CASCHED = 8`
- **缓存感知机制**：维护任务历史表与加速表，记录 `last_cpu`、`per_cpu_recency` 等状态
- **核心算法**：基于 Cache Recency Profile（CRP）预测执行时间，结合 LCIF/MSF 策略做出（作业，核心）分配决策
- **工程实用性**：完整集成到 Linux 6.8 调度框架，可在 QEMU 中启动验证

## 目录结构

```
Yat-CAShed/
├── README.md                    # 本文件
├── linux-6.8-main/              # Linux 6.8 内核源码 + Yat_Casched 调度器
│   ├── kernel/sched/yat_casched.c          # 调度器核心实现
│   ├── include/linux/sched/yat_casched.h   # 调度器头文件
│   ├── Yat_Casched开发文档.md               # 完整实现文档
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

编译与运行：

```bash
# Windows
.\build.bat run        # 编译 + 实验 + 可视化

# Linux / macOS
make run               # 编译 + 实验 + 可视化
make picture           # 单独生成可视化图表
```

完整说明见 [`Simulator.md`](simulator/Simulator.md)。

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

## 主要依赖

- **内核**：GCC/Binutils、bison、flex、libssl-dev、libelf-dev、bc、dwarves、QEMU、busybox
- **模拟器**：JDK 17+，第三方 JAR 位于 `simulator/lib/`（JGraphT、commons-math3、fastutil 等）
- **可视化**：Python 3 + pandas / seaborn（可选，缺失时自动降级）

## 许可证

内核部分遵循 Linux 内核的 GPL-2.0 许可证（见 [`linux-6.8-main/COPYING`](linux-6.8-main/COPYING)）；模拟器与文档的学术使用权以各参考论文与项目说明为准。
