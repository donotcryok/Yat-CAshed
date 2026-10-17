# 更新日志（CHANGELOG）

本项目自本文件引入之日起采用 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 风格记录变更。
日期格式为 `YYYY-MM-DD`。

## [Unreleased] — 工程化整理

### 新增（Added）

- **内核调度器冲突解决（路线 A，决赛 AJLR 版）**：
  [`linux-6.8-main/kernel/sched/yat_casched.c`](linux-6.8-main/kernel/sched/yat_casched.c)
  由 **1791 行（含 10 处 Git 冲突标记）** 重建为 **约 1396 行、0 标记** 的单一 AJLR 实现：
  - 10 处冲突全部取 `yat/yat_casched-dev`（决赛版）侧；整段移除初赛版残留的
    `update_accelerator_table()` / `schedule_yat_casched_tasks()` / `struct accelerator_entry` /
    静态哈希 `accelerator_table` / `struct history_record` 等 v2.2 体系。
  - 调度入口统一为 `balance_yat_casched()` 在 **CPU0** 调用 `yat_schedule_ajlr(&rq->yat_casched)`。
  - `put_prev_task` 的历史记录改调 dev 版 `yat_add_history_record(cpu, job_id, now, delta)`。
  - 文件行尾由 CRLF 统一为 **LF**。
  - 决策与执行明细见 [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md) §7–§8。
- [`linux-6.8-main/include/linux/sched/yat_casched.h`](linux-6.8-main/include/linux/sched/yat_casched.h)：
  补齐 3 条外部链接声明（`yat_create_job_for_task` / `yat_complete_task_job` / `yat_create_test_dag`），
  头文件声明函数数达 **33**，「接口泄漏」清单清零。
- [`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md)：**模拟器实验报告**——含实验目的、
  环境、参数与任务集生成方法、算法/CRP 模型公式、指标定义、总体与分利用率统计、
  4 例失败案例分析、图表说明、58 项单测覆盖、有效性威胁（模型内建缓存收益、
  WFD 命中率截断估算、单种子无显著性检验等）与复现命令。

- **文档体系**：新增 `docs/` 目录，包含文档索引、架构说明、目录详解、开发指南、
  冲突分析、路线图与归档说明。
  - [`docs/README.md`](docs/README.md)
  - [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
  - [`docs/DIRECTORY.md`](docs/DIRECTORY.md)
  - [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)
  - [`docs/CONFLICT_ANALYSIS.md`](docs/CONFLICT_ANALYSIS.md)
  - [`docs/ROADMAP.md`](docs/ROADMAP.md)
  - [`docs/archive/README.md`](docs/archive/README.md)
- **顶层编排**：[`Makefile`](Makefile)（`make help` / `check` / `symbol-check` / `simulator*` / `visualize` / `tree` / `clean`）。
- **仓库运维脚本**：[`scripts/check_repo.sh`](scripts/check_repo.sh)（冲突标记 / 空文件 / 关键文件健康检查）。
- **符号一致性检查脚本**：[`scripts/check_symbols.sh`](scripts/check_symbols.sh)
  （核对 `yat_casched.h` 声明与 `yat_casched.c` 定义；容忍已知待决策项，暴露新增缺口）。
  - 区分 `static`（文件内私有）与外部链接，避免把私有函数误报为「接口泄漏」；
  - 新增检查 **[C] 链接属性一致性**：捕获「`static` 原型 ↔ 非 `static` 定义」的编译级缺陷。
- **模拟器单元测试**：[`simulator/tests/YatSimulatorTest.java`](simulator/tests/YatSimulatorTest.java)
  （零依赖、无需 JUnit）——覆盖 `Node` 实体约束、`RecencyProfileReal` 的
  L1/L2/L3/内存命中→执行时间映射、`WFD` 最差适配与重置、`CacheAware_v2` 契约，共 30 项断言。
  通过 `make -C simulator test` 运行，并已纳入 `verify` 与 CI。
- **CRP / Speedup-Benefit 边界测试**：在单元测试中新增 `[2b]` 与 `[5]` 两组共 28 项断言，
  覆盖：缓存级别**优先级**（L1>L2>L3 同时命中）、**访问窗口**边界（第 5/6 近访问）、
  **数值截断**（`base=1` 的 `(long)(1*0.8)=0`/`(long)(1*1.2)=1`）、**零基**（`base=0`）、
  **故障叠加**（命中×1.5）、Speedup/Benefit 的 **灵敏度阈值**（严格 `>0.5`）、
  **算法名精确匹配**差异、Makespan/负载均衡/CPU 利用率的**确定性**与**顺序无关（对称）性**。
  断言总数由 30 提升至 **58**。
- **依赖审计报告**：[`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md)
  （`jdeps` + 源码 `import` 双重审计）：`simulator/lib/` 共 30 个 JAR（约 77 MB），
  仅 `commons-math3-3.6.1.jar`（约 2.2 MB）被真实依赖，其余 29 个（≈97%）为死依赖。
  **该 29 个死依赖已在独立变更中实际移除**（见「移除」小节）。
- **跨平台模拟器构建**：[`simulator/Makefile`](simulator/Makefile) 由 Windows `cmd` 语法改写为
  可移植 GNU Make（自动识别 OS、JDK、类路径分隔符），保留 `build.bat` 作为 Windows 入口。
- **可复现实验参数化**：`AlgorithmComparisonExperiment` 支持 `--cores/--cases/--tasks/--seed/--no-viz`
  命令行参数；修复用例在利用率等级间的**整数除零/分布不均**问题；随机种子可复现。
- **贡献规范**：[`CONTRIBUTING.md`](CONTRIBUTING.md)（分支模型、提交规范、代码风格、文档同步要求）。
- **许可证说明**：[`LICENSE`](LICENSE)（分模块说明）。
- **持续集成**：[`.github/workflows/ci.yml`](.github/workflows/ci.yml)（仓库健康检查 + 模拟器编译校验）。
- **编辑器统一配置**：[`.editorconfig`](.editorconfig)。

### 变更（Changed）

- [`README.md`](README.md)：新增「文档导航」与冲突警示，补充顶层工程文件说明；
  **CI 徽章 OWNER 占位符替换为 `mafuquan`**（`https://github.com/mafuquan/Yat-CAShed`）。
- [`README.md`](README.md)：`yat_casched.c` 的冲突警示由「⚠️ 未解决」改为「✅ 已按路线 A 解决」，
  并补充 [`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md) 导航项。
- [`scripts/check_repo.sh`](scripts/check_repo.sh)：**移除 `KNOWN_CONFLICT_FILES` 冲突白名单**——
  冲突已全部解决，此后任何冲突标记（含 `yat_casched.c`）一律阻断。
- [`.github/workflows/ci.yml`](.github/workflows/ci.yml)：冲突守卫同步取消白名单，
  输出文案改为「✅ 未发现任何冲突标记」。
- 同步更新文档中过时的冲突描述：[`docs/README.md`](docs/README.md)、
  [`docs/DIRECTORY.md`](docs/DIRECTORY.md)、[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)、
  [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)、[`docs/ROADMAP.md`](docs/ROADMAP.md)、
  [`CONTRIBUTING.md`](CONTRIBUTING.md)。

### 修复（Fixed）

- `linux-6.8-main/kernel/sched/yat_casched.c`（路线 A 重建时一并修复的连带缺陷）：
  - **`yat_pool_lock` 丢失**：该锁原定义在被丢弃的 HEAD 冲突块内，而 dev 侧 `enqueue`/`dequeue`
    仍在引用 → 补回 `static DEFINE_SPINLOCK(yat_pool_lock);`，并补兼容用的
    `static LIST_HEAD(yat_ready_job_pool);`。
  - **`dequeue_task_yat_casched` 加锁未解锁**：dev 侧只有 `spin_lock()` 没有 `spin_unlock()`，
    且缺少 `nr_running--` → 补齐解锁与计数递减。
  - **`yat_ajlr_dequeue_task` 链接属性不一致**：原型为 `static`、定义缺 `static`，
    GCC 会报 `non-static declaration follows static declaration` → 定义补 `static`。
  - **缺失头文件**：补 `<linux/cpumask.h>`（`for_each_online_cpu`）、`<linux/smp.h>`
    （`smp_processor_id`）、`<linux/limits.h>`（`INT_MAX` / `ULLONG_MAX`）。
- `linux-6.8-main/kernel/sched/sched.h`：移除重复的
  `#include <linux/sched/yat_casched.h>`（原第 23 行与第 76 行重复）。
- `simulator/AlgorithmComparisonExperiment.java`：可视化调用的 Python 脚本名
  由不存在的 `visualizer/visualize_results.py` 修正为实际的 `visualizer/visualize.py`。
- `simulator/AlgorithmComparisonExperiment.java`：修正 `runPythonVisualization()` 中
  **Python 解释器选择缺陷**——原逻辑在 `python` 失败、`python3` 成功后仍以
  `checkResult == 0 ? "python" : "python3"` 误选回 `python`，导致可视化静默失败；
  改为按 `python3` → `python` 顺序探测并记录**成功命令**，同时转发脚本输出以便排错。
- `simulator/visualizer/ResultVisualizer.java`：修正 `generateImprovementSummary()` 中
  **改进方向符号反转**的缺陷——原实现以 `CacheAware_v2` 为分母、且缓存命中率作差方向相反，
  导致虽算法更优却输出「Makespan: 增加 13.53%」「缓存命中率: 降低 30.21pp」；
  改为以 `WFD` 为基准、正值表示改进，并区分「改进/劣化」「提升/降低」。
  修复后输出「Makespan: 改进 11.92%」「缓存命中率: 提升 30.21 个百分点」。
- `simulator/visualizer/visualize.py`：修复在 **matplotlib ≥ 3.9/3.11** 下
  `Axes.boxplot(labels=...)` 已被移除导致的 `TypeError`（图表无法生成）；
  改用 `set_xticks` + `set_xticklabels`，兼容新旧 matplotlib。
- `simulator/Makefile`：修正 `MKDIR_P` / `RM_RF` 因 `:=` 立即展开导致 `$(1)` 为空、
  进而 `mkdir ''` 失败的问题（改用递归 `=` 与 `$(call ...)`）。
- 顶层 `Makefile`：补齐 `.PHONY` 中已声明但缺失实现的 `simulator-test` 目标。

### 移除（Removed）

- **移除 29 个未使用 JAR 依赖**（`simulator/lib/`，约 75 MB）：
  移除前先跑 `make simulator-verify` 建立基线，移除后 `58/58` 断言通过、
  全量实验输出**逐位一致**；`lib/` 体积由 **77 MB → 2.2 MB**，
  仅保留 [`commons-math3-3.6.1.jar`](simulator/lib/commons-math3-3.6.1.jar)。
  清单与审计方法见 [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md)。
  29 个 JAR 均已被 git 跟踪，可随时从历史回滚。
- 删除 0 字节空文件：
  - `linux-6.8-main/boot_test_scripts/yat_scheduler_test.c`
  - `linux-6.8-main/boot_test_scripts/test_simple_history_validation.c`
  - `report/paper/Yat-CAShed_new.tex`

### 验证（Verified）

- `bash scripts/check_repo.sh` → 退出码 0，冲突标记检查输出「✅ 未发现冲突标记」。
- `bash scripts/check_symbols.sh` → 退出码 0，**白名单已全部清空**：
  头文件声明 26 / 非 static 定义 26 / static 定义 17；
  [A] 无非预期缺失、[B] 无非预期泄漏、[C] 无非预期不一致。
- **内核真实编译验证通过 —— 且工作区本身已可直接编译**：
  - 配置：`tinyconfig` + `CONFIG_SMP=y` + `CONFIG_SCHED_SMT=y` + `CONFIG_SCHED_MC=y`
    + `CONFIG_SCHED_CLASS_YAT_CASCHED=y`。
  - `make ARCH=x86_64 -j12 kernel/sched/yat_casched.o` → **14136 字节，0 error / 0 warning**。
  - `make ARCH=x86_64 -j12 vmlinux` → **11856568 字节，0 error / 0 warning**；
    `System.map`（527522 字节）含 33 个 `yat_*` 符号，包括 `yat_casched_sched_class`、
    `select_task_rq_yat_casched`、`pick_next_task_yat_casched` 等。
  - 顺带修掉 2 个 `-Wunused-function`：`yat_ready_pool_remove()` 接入出队路径
    （这是一个真实的漏调度缺陷）、`yat_ready_pool_destroy()` 标记 `__maybe_unused`。
  - 注：`CONFIG_SMP=y` 为硬性前提（`sched_class` 的 `balance`/`select_task_rq`/`pick_task`
    等成员与 `rq::cpu` 均在 `#ifdef CONFIG_SMP` 内）。
- **仓库完整性修复**（详见 [docs/CONFLICT_ANALYSIS.md](docs/CONFLICT_ANALYSIS.md) §8.6）：
  - 新增 [`/.gitattributes`](.gitattributes)：`* text=auto eol=lf` + 二进制白名单，
    并把内核树内的二进制例外排在通用规则**之后**（`gitattributes` 是后者覆盖前者）。
  - 新增 [`scripts/normalize_eol.py`](scripts/normalize_eol.py)：全树 CRLF→LF，
    `converted=81146 binary_skipped=4 failed=0`。
  - 从上游 linux-6.8.0 补齐 **1487 个**丢失文件（1326 `.S` + 110 `.tc` + 22 `.log`
    + 9 `.h` + 5 `.c` + 4 `.py` + 3 `.pm` + 其余），并删除 1 个 0 字节占位 `.tex`。
  - 恢复 **1225 个** shebang 脚本的可执行位（`100755` 入索引，clone 后自动保留）。
  - 修掉 `.gitignore` 的**四类误伤**（详见 §8.6.2）：`*.s`→`.S`、`*.d`→`test.d`/`conf.d` 目录、
    `tags`→`selftests/arm64/tags/` 目录、Yat 自定义 `*.log`→kunit `test_data/*.log`。
    其中前三类都属于「glob 匹配到目录后，`!` 无法救回其下文件」的陷阱，必须先 negate 目录。
  - **全新 clone 直接编译验证**：`git clone --no-hardlinks` 到 `/tmp/freshclone`，
    `git ls-files | wc -l` = 83568、`.S` = 1326、`.tc` = 116、残留 CRLF = 0、
    `scripts/as-version.sh` 权限 `-rwxr-xr-x`；随后 `make tinyconfig` → `make vmlinux`
    **一次通过，0 error / 0 warning**。即不再需要任何 `/tmp` 副本或手工修复。
- `make -C simulator test` → **58 项断言全部通过**。
- `make -C simulator verify` → 编译通过、依赖自检通过、单测通过、快速实验通过。
- `make -C simulator run-quick`（种子 42）：Makespan 平均改善 **17.36%**，
  缓存命中率平均改善 **31.81 个百分点**，`CacheAware_v2` 胜率 **100%**。
- **全量实验** `make -C simulator run`（100 用例 / 60 任务 / 种子 42）跑通：
  平均 Makespan 改进 **11.66%**、平均缓存命中率改进 **30.21 个百分点**
  （73.8% vs 43.6%）、`CacheAware_v2` 胜率 **96/100 = 96.0%**；
  3 张 300 dpi 图表（`makespan_analysis.png` / `cache_hit_analysis.png` /
  `execution_time_and_win_rate_analysis.png`）成功输出至 `simulator/result/`。
- 可视化修复后重跑全量实验（启用 viz）：`[viz]` 报告输出
  「Makespan Reduction +11.9% / Cache Hit +30.2% / Execution Time +14.0%」，
  3 张 PNG 均重新生成；`ResultVisualizer` 修正后输出
  「Makespan: 改进 11.92%」「缓存命中率: 提升 30.21 个百分点」（此前为方向反转的错误值）。
- `make check` / `make symbol-check` 均退出码 0。
- CI（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）已接入上述全部检查与模拟器回放。

### 已知遗留（Known Issues）

- ~~`yat_global_pool_*`（7 个）声明未实现~~ → **已从接口删除**
  （全树 grep 确认 `struct yat_global_task_pool`/`yat_global_task_node` 零引用、
  7 个函数零调用点；实际路径为 `global_ready_pool`）。`check_symbols.sh` 白名单同步清空。
- ~~整树 CRLF 未在仓库内修正~~ → **已修复**：81146 个文件转 LF，`.gitattributes`
  固化规则；1225 个脚本补 `100755`；1326 个 `.S` + 110 个 `.tc` + 其余共 1487 个
  文件已补齐入库。**全新 clone 直接 `make vmlinux` 一次通过**（0 error / 0 warning）。
  详见 [docs/CONFLICT_ANALYSIS.md](docs/CONFLICT_ANALYSIS.md) §8.6。
- **历史提交仍含 CRLF**：本次只修正了 HEAD 之后的内容；若需彻底清理历史，
  需 `git filter-repo` 之类的重写（会影响所有 fork，暂不做）。
- **锁序需审查**：`balance_yat_casched()`（CPU0）在持 `yat_pool_lock` 时再 `rq_lock(target)`，
  与 `global_dag_lock` / `yat_accel_table.lock` 的相对顺序需用 `lockdep` 验证
  （见 [`docs/ROADMAP.md`](docs/ROADMAP.md) A5）。
