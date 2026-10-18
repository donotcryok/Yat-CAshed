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

## 项目主页

- 🌐 **在线主页**：<https://donotcryok.github.io/Yat-CAShed/>（源码见 [`docs/index.html`](docs/index.html)）
  内容：项目定位、决策链路、实测结果、快速开始与文档导航。
  **首次使用需在仓库 Settings → Pages 把 Source 设为 `main` 分支的 `/docs` 目录**
  （仓库内已放好 `docs/.nojekyll`，不启用 Jekyll；页面是纯静态单文件、无外部字体/CDN，
  也可直接下载后用浏览器打开 `docs/index.html` 预览）。

## 文档导航

工程化文档统一收敛在 [`docs/`](docs) 目录，建议按以下顺序阅读：

| 文档 | 说明 |
|------|------|
| [`docs/README.md`](docs/README.md) | 文档总索引（入口） |
| [`docs/PROJECT_AUDIT.md`](docs/PROJECT_AUDIT.md) | 工程化审计快照（现状 / 问题清单 / 处理状态） |
| [`docs/DIRECTIONS.md`](docs/DIRECTIONS.md) | 改进与开发方向建议（候选方向 + 推荐路线） |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 系统架构与模块说明 |
| [`docs/DIRECTORY.md`](docs/DIRECTORY.md) | 目录结构详解 |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | 环境搭建与构建指南 |
| [`docs/OPERATIONS.md`](docs/OPERATIONS.md) | **操作手册**（编译 / 验证 A8 / 读计数器 / 发补丁，照着敲即可） |
| [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md) | 合并冲突分析与解决记录（已按路线 A 解决） |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | 任务化改进清单（P0–P3） |
| [`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md) | 模拟器实验报告（指标 / 统计 / 复现） |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | 贡献规范（分支 / 提交 / 代码风格） |

> ✅ **状态**：`kernel/sched/yat_casched.c` 的 **10 处 Git 合并冲突已按「路线 A（决赛 AJLR 版）」全部解决**，
> 当前文件无冲突标记、符号一致性检查通过。决策依据与执行明细见 [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md) §7–§8。

## 目录结构

```
Yat-CAShed/
├── README.md                    # 本文件
├── LICENSE / CONTRIBUTING.md    # 许可证说明 / 贡献规范
├── CHANGELOG.md                 # 变更日志
├── Makefile                     # 顶层编排（check / docs-check / simulator / report）
├── .editorconfig / .gitattributes / .gitignore
├── docs/                        # 工程化文档体系（入口见 docs/README.md）
├── scripts/                     # 仓库运维脚本
│   ├── check_repo.sh / check_symbols.sh / check_docs.py / check_names.py   # 门禁
│   ├── kernel-patch.sh / gen_kernel_patches.sh                            # 补丁应用/生成
│   ├── setup-kernel.sh                                                    # 从上游 v6.8 重建内核树
│   └── normalize_eol.py                                                   # 行尾归一化
├── .github/workflows/ci.yml     # CI（仓库检查 + 文档链接 + 命名 + 符号 + 补丁完整性 + 模拟器回放）
├── linux-6.8-main/              # Linux 6.8 内核源码 + Yat-CAShed 调度类
│   ├── kernel/sched/yat_casched.c          # 调度器核心实现（AJLR，冲突已解决）
│   ├── include/linux/sched/yat_casched.h   # 调度器头文件
│   ├── yat_casched优化记录.md               # 迭代优化记录
│   ├── boot_test_scripts/                   # 用户态测试程序
│   ├── yat_test_scripts/                    # QEMU 测试环境脚本
│   └── test_visualization/                  # 性能测试与可视化
├── simulator/                   # 缓存感知调度算法模拟器（Java 17+）
│   ├── Simulator.md                          # 模拟器详细文档
│   ├── AlgorithmComparisonExperiment.java    # 实验入口
│   ├── allocation/                           # WFD / CacheAware 算法实现
│   ├── tests/                                # 单元测试（102 断言）
│   └── lib/                                  # commons-math3（唯一运行时依赖）
└── report/                      # 论文、报告与参考资料
    ├── paper/                    # 毕业论文 LaTeX 源码（章节文件平铺）
    ├── prompt_md/                # 分析笔记与计划
    └── *.pdf / *.pptx            # 参考论文与早期材料
```

## 核心组件

### 1. 内核调度器（[`linux-6.8-main/`](linux-6.8-main)）

在 Linux 6.8 内核上新增 `SCHED_YAT_CASCHED`（策略号 8）调度类，主要改动点：

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

> 架构与集成点说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)，迭代记录见 [`yat_casched优化记录.md`](linux-6.8-main/yat_casched优化记录.md)，
> 初赛版/决赛版的**历史**实现文档已归档到 [`docs/archive/`](docs/archive)。

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
make -C simulator test      # 单元测试（102 断言，零依赖）
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
- 参考论文：AJLR（RTNS'23）、Cache-Aware Allocation of Parallel Jobs on Multi-cores
- [`report/prompt_md/`](report/prompt_md)：分析与计划笔记
- 初赛版/决赛版**历史**实现文档：[`docs/archive/`](docs/archive)（不再是事实来源）

## 构建与测试（内核部分）

> 内核编译需在 Linux 环境（或 WSL）下进行，Windows 无法完整检出部分 Linux 专属文件名。

```bash
# 0) 最快的语法/类型检查（只编调度器目标文件，约 1 分钟）
make kernel-obj

# 1) 编译内核（产物 arch/x86/boot/bzImage）
make kernel-build

# 2) 验证 A8：AJLR 是否真的接管了选核
#    两个参数缺一不可：none（可跨核）+ sleep-us（制造 WF_TTWU）
linux-6.8-main/yat_test_scripts/compare_cfs_vs_yat.sh \
    --tasks 8 --iters 2000000 --affinity none --sleep-us 200 --timeout 300
```

实测基线（8 任务 / 4 核 / 2M 迭代 / 200µs 唤醒）：`ttwu_ok 512`、
`select_decided 254`、`jobs_placed 254`、`place_hit 925 / miss 0 / pending 14`
→ **接管率 98%**；但墙钟约为 CFS 的 **1.9–2.2 倍**，即「AJLR 已生效、性能尚未站住」。

完整的命令清单、计数器读法与故障排查见
[`docs/OPERATIONS.md`](docs/OPERATIONS.md)；测试脚本参数见
[`linux-6.8-main/yat_test_scripts/README.md`](linux-6.8-main/yat_test_scripts/README.md)。
用户态测试程序位于 [`boot_test_scripts/`](linux-6.8-main/boot_test_scripts)。

## 仓库健康检查

```bash
make check        # 冲突标记 / 空文件 / 关键文件
make docs-check   # Markdown 相对链接有效性
make symbol-check # 内核头文件声明 vs 实现定义
make verify-all   # 以上 + 模拟器全套验证（CI 等价）
```

- [`scripts/check_repo.sh`](scripts/check_repo.sh)：冲突标记、空文件、关键文件守卫
- [`scripts/check_docs.py`](scripts/check_docs.py)：全仓库自维护 Markdown 的相对链接检查
- [`scripts/check_symbols.sh`](scripts/check_symbols.sh)：核对 `yat_casched.h` 声明与 `yat_casched.c` 定义
  （当前 29 个声明 / 27 个非 static 定义 / 28 个 static 定义，A/B/C 三类不一致均为 0）
- 三者均已接入 CI（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）

论文编译：

```bash
make report      # 优先 latexmk -xelatex，产物 report/paper/Yat-CAShed.pdf
```

## 主要依赖

- **内核**：GCC/Binutils、bison、flex、libssl-dev、libelf-dev、bc、dwarves、QEMU、busybox
- **模拟器**：JDK 17+（`openjdk-17-jdk-headless` 即可）；
  运行时依赖仅 [`simulator/lib/commons-math3-3.6.1.jar`](simulator/lib)（详见 [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md)）
- **可视化**：Python 3 + matplotlib（必需）/ pandas、seaborn（可选，缺失时自动降级）；安装 `pip install -r simulator/visualizer/requirements.txt`
- **论文**：TeX Live（`xelatex`、`ctex`、`latexmk` 可选）

## 许可证

分模块授权，详见 [`LICENSE`](LICENSE)：

- **内核部分**（`linux-6.8-main/` 中新增/修改的调度器文件）：GPL-2.0，与 Linux 内核一致（见 [`linux-6.8-main/COPYING`](linux-6.8-main/COPYING)）。
- **模拟器**（`simulator/` 自有 Java 源码）：学术研究与教学使用，保留所有权利。
  第三方依赖 `simulator/lib/commons-math3-3.6.1.jar` 归其作者所有（Apache-2.0）。
- **文档与论文**（`docs/`、`report/` 中本项目撰写部分）：学术研究与教学使用，转载请注明出处。
  `report/` 内引用的第三方论文 PDF 版权归原作者，仅作学习研究。
