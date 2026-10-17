# Yat-CAShed 改进与开发方向（Roadmap）

> 本文给出从「当前状态」到「工程化、可复现、可发表」的几个明确方向，按优先级与投入分层。
> 其中 **P0** 为阻断性问题，必须先解决才能继续。

---

## 0. 优先级总览

| 层级 | 方向 | 目标 | 预估投入 |
|------|------|------|---------|
| ~~**P0**~~ | ~~解决 `yat_casched.c` 合并冲突~~ | ~~恢复可编译~~ | ✅ **已完成（路线 A）** |
| **P1** | 文档与目录工程化 | 单一事实来源、新人可上手 | 1–2 天 |
| **P1** | 构建与 CI | 跨平台可构建 + 冲突守卫 | 0.5–1 天 |
| **P2** | 内核模块质量 | 可测试、可观测、内存/锁安全 | 1–2 周 |
| **P2** | 模拟器工程化 | 标准化构建、可复现实验 | 3–5 天 |
| **P3** | 算法与研究深化 | 论文与对比实验支撑 | 持续 |

---

## 1. 方向 A：内核调度器（`linux-6.8-main/`）

### A1. 恢复可编译（P0）✅ 已完成（含真实编译验证）
已按 **路线 A（决赛 AJLR 版）** 解决 10 处冲突：文件由 1791 行重建为约 1396 行、0 标记，
并移除 HEAD 侧残留的 v2.2 全局调度逻辑与重复符号。

**真实内核编译已验证通过**（`/tmp/linbuild` 独立副本）：
`kernel/sched/yat_casched.o` = 14136 字节、0 error / 0 warning；
`vmlinux` = 11856568 字节、0 error；`System.map` 含 56 个 `yat_*` 符号。
前置条件：`CONFIG_SMP=y`（否则 `sched_class` 的 `balance`/`select_task_rq`/`pick_task`
等成员与 `rq::cpu` 不存在），且需先把整树 CRLF 转 LF、补齐 125 个脚本可执行位。

详见 [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §7、§8.4、§8.5。

### A2. 消除“声明/实现不一致”（P1）✅ 已完成
- ✅ 已引入 [`scripts/check_symbols.sh`](../scripts/check_symbols.sh) 并接入 CI，静态比对
  「头文件声明 vs `.c` 定义」，区分 `static` 与外部链接，检查三类问题：
  [A] 声明未实现、[B] 接口泄漏、[C] 链接属性不一致。
  - 已捕获并**修复** 1 处编译级缺陷（`yat_ajlr_dequeue_task` 的 static 声明↔非 static 定义）。
  - 接口泄漏（`yat_create_job_for_task` / `yat_complete_task_job` / `yat_create_test_dag`）
    已转为头文件正式声明，泄漏清单清零。
  - ✅ `yat_global_pool_*`（7 个）**已从接口移除**：全树 grep 确认 `struct yat_global_task_pool` /
    `struct yat_global_task_node` 零引用、7 个函数零调用点，实际生效路径是 `global_ready_pool`。
    同步删除 `struct yat_casched_rq::global_pool` 字段。
  - ✅ **白名单已全部清空**（`KNOWN_MISSING_RE` / `KNOWN_LEAK_RE` / `KNOWN_LINKAGE_RE` 均为 `^$`），
    声明 26 / 非 static 定义 26 / static 定义 17，三类检查均无非预期项。
  - 附带修掉 2 个 `-Wunused-function`：`yat_ready_pool_remove()` 接入 `yat_ajlr_dequeue_task()`
    （修复「任务已睡眠但作业仍留在就绪池被反复选中」的漏调度），
    `yat_ready_pool_destroy()` 标记 `__maybe_unused`。

### A3. 将调度器产出一份「纯补丁」（P1，强烈建议）
当前改动**散落在 8+ 个标准内核文件**中，难以评审与回溯。建议：
- 在内核树内维护 `yat_casched.patch`（`git format-patch` 生成），
- 或改为**独立模块化调度类**（若可行）以减少对 `core.c` / `sched.h` 的侵入。
- 好处：审稿人/同伴能一眼看清“到底改了什么”，也便于 rebase 到新内核版本。

### A4. 修复脆弱的内核集成点（P2）
- `include/asm-generic/vmlinux.lds.h:135` 手工插入 `__yat_casched_sched_class` 段，
  顺序错误会被 `core.c:9951` 的 `BUG_ON` 直接 panic。建议加注释与编译期断言说明约束。
- `kernel/sched/sched.h` 重复 include（已修一处），建议通查。
- `init/Kconfig` 的配置项**缩进用了空格**而非 Tab，且位置不在 `kernel/sched/Kconfig`，风格不符内核规范。

### A5. 并发与内存安全审查（P2）
重点审查 dev 版的全局单点调度路径：
- `balance_yat_casched()`（CPU0）→ `schedule_yat_casched_tasks()` → `rq_lock(target_rq)`；
- `yat_pool_lock`、`global_dag_lock`、`yat_accel_table.lock` 与 `rq_lock` 的**加锁顺序**；
- `add_history_record()` 使用 `GFP_ATOMIC` 但可能被非原子上下文调用；
- `yat_accel_table_insert()` 与 `yat_find_best_core_for_job()` 每次调度遍历所有 CPU 并在原子上下文
  `kmalloc`（`GFP_ATOMIC`）——**性能与内存压力**隐患。
- 建议：引入 `lockdep` 测试 + `KASAN/Kmemleak` 跑一轮。

### A6. 可观测性（P2）
- 用 `tracepoint` / `trace_printk` 替代散落的 `printk(KERN_INFO ...)`（当前会被刷屏、影响调度时序）。
- 暴露 `/proc/sched_yat_casched` 或 `debugfs` 节点，输出历史表、加速表命中、迁移计数等。

### A7. 测试体系（P2）
- ~~`boot_test_scripts` 有**空文件**（`yat_scheduler_test.c`、`test_simple_history_validation.c`）~~ ✅ 已在本次工程化整理中删除。
- 统一两套测试目录（`boot_test_scripts` 与 `yat_test_scripts`）职责：
  建议 `boot_test_scripts` 只放 C 测试源码，`yat_test_scripts` 只放 QEMU/initramfs 脚本。
- 增加“同一负载 CFS vs Yat_Casched”自动对比脚本，产出 CSV。

---

## 2. 方向 B：模拟器（`simulator/`）

### B1. 跨平台构建（P1）✅ 已完成
[`simulator/Makefile`](../simulator/Makefile) 已由 **cmd.exe 语法**改写为**跨平台 GNU Make**：
- 自动检测 `OS`，选择 `:`/`;` classpath 分隔符与 `mkdir -p`/`rm -rf`；
- 支持 `JAVA_HOME` 覆盖，保留 `build.bat` 作为 Windows 原生入口；
- 目标：`compile / lib-test / test / run / run-quick / verify / picture / clean`。
- ⏳ 仍未迁移到 Maven/Gradle（依赖声明化），列为后续可选优化。

### B2. 依赖治理（P1）✅ 已完成
`simulator/lib/` 原 **30** 个 JAR（约 77 MB），经 `jdeps` + 源码 `import` 双重审计：
- ✅ 结论：**仅 `commons-math3-3.6.1.jar`（约 2.2 MB）被真实依赖**（提供 `Pair`），
  其余 **29 个（≈97%）为死依赖**（含 `jython` 42 MB、`fastutil` 23 MB 等）。
- ✅ **已移除 29 个死依赖**：`lib/` 由 77 MB → **2.2 MB**。移除前跑通
  `make simulator-verify` 作为基线，移除后 58/58 断言通过、全量实验数值**逐位一致**。
  29 个 JAR 均已被 git 跟踪，可随时从历史回滚。
- ✅ 详见 [DEPENDENCIES.md](DEPENDENCIES.md)（审计命令、已移除清单、变更记录）。
- ⏳ 可选后续：以自实现 `Pair` 完全删除 `commons-math3`，达成零第三方依赖。

### B3. 可复现实验（P2）✅ 基本完成
- ✅ 随机种子已显式化并可通过 `--seed=` 指定；默认种子 42。
- ✅ 实验参数已外置为命令行参数：`--cores= / --cases= / --tasks= / --seed= / --no-viz`，
  并修复了用例在利用率等级间分布不均的整数除法缺陷（见 [`AlgorithmComparisonExperiment.java`](../simulator/AlgorithmComparisonExperiment.java)）。
- ✅ `make -C simulator run-quick` 产出可复现的基准结果（种子 42：Makespan −17.36%、命中 +31.81pp、胜率 100%）。
- ✅ **全量实验（100 用例 / 60 任务 / 种子 42）已跑通**：平均 Makespan 改进 11.66%、
  缓存命中率改进 30.21pp（73.8% vs 43.6%）、胜率 96/100；并产出 3 张 300 dpi 图表。
  同时修复了 Python 解释器选择缺陷、matplotlib ≥3.9 `boxplot(labels=)` 移除导致的图表生成失败，
  以及 `ResultVisualizer` 改进方向符号反转（曾误报「Makespan 增加 13.53%」）的缺陷。
- ✅ **实验报告已落地**：[EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md)（设计 / 参数 / 公式 /
  总体与分利用率统计 / 失败案例分析 / 有效性威胁 / 复现命令）。
- ⏳ 待办：把参数外置为 `config/*.yaml`；统一 CSV schema 与图表命名文档；
  用多种子重复实验并给出置信区间与显著性检验。

### B4. 流水线整合（P2）✅ 已完成
- ✅ `run` 目标中的 Windows 命令已消除，改为跨平台实现。
- ✅ 新增**零依赖单元测试** [`simulator/tests/YatSimulatorTest.java`](../simulator/tests/YatSimulatorTest.java)（58 断言），
  覆盖 Node 约束、CRP 的 L1/L2/L3/内存映射及**边界条件**（同时命中、跨 DAG、访问窗口、
  故障叠加、`(long)` 截断）、WFD 与 CacheAware_v2 契约、Speedup/Benefit 的阈值与对称性。
- ✅ CI（[`.github/workflows/ci.yml`](../.github/workflows/ci.yml)）只跑 `compile → lib-test → test → run-quick`，
  可视化（依赖 pip）不进 CI；可视化调用可视环境自动降级。
- ✅ `requirements.txt` 已就位，`docs/DEVELOPMENT.md` 补充了 Python 依赖说明。

---

## 3. 方向 C：文档与仓库治理（P1）

- **统一命名**：确定唯一项目名（建议 `Yat-Casched`），全仓库统一，避免 `Yat_Casched`/`Yat_CAShed`/`YAT_CASCHED` 混用。
- **单一事实来源**：两份“完整实现文档”合并为一份（按冲突路线的最终选择更新），另一份标注 `[已归档]` 并移至 `docs/archive/`。
- **变更日志**：把 `yat_casched优化记录.md` 迁移为规范的 `CHANGELOG.md`（带版本号与日期）。
- **LICENSE**：内核部分 GPL-2.0，模拟器/文档需明确授权（当前 `README` 只写了“学术使用权以参考论文为准”，法律上不清晰）。
- **CONTRIBUTING.md**：提交规范（当前 git log 已用 `feat:/fix:/chore:` 风格，可固化）、分支模型、PR 流程。
- **CI**：至少保证①无冲突标记 ②模拟器可编译 ③（可选）文档链接检查。

---

## 4. 方向 D：算法与研究深化（P3）

以下几种是在现有基础上**自然延伸**、且能与论文形成呼应的开发方向：

1. **自适应缓存热度窗口**：初赛版固定 10ms。可基于 workload 特征（迁移频率、cache 命中反馈）
   在线调整窗口，形成「自适应 AJLR」。
2. **CRP 模型数据驱动化**：当前 `get_crp_ratio()` 是手写分段函数（桩）。
   可改用**离线 profile 拟合的真实 CRP 曲线**（论文 RTNS'23 的方法），显著提升说服力。
3. **NUMA 感知扩展**：`calculate_recency()` 已区分 L1/L2/L3，可进一步引入 NUMA 距离矩阵，
   把“缓存亲和”升级为“缓存 + 内存带宽亲和”。
4. **与 EAS / schedutil 协同**：把缓存感知收益纳入频率/能耗决策，做能效（perf/Watt）对比。
5. **实时性分析**：项目资料含大量 DAG/实时调度理论（`chapter2/4`）。
   可对 AJLR 版做**可调度性分析**（response-time / makespan bound），而不止实验对比。
6. **模拟器 ↔ 内核一致性验证**：让 Java 模拟器与内核调度器在**同一组 trace 输入**下产出可对比的决策序列，
   这是很强的论文贡献点（“仿真可复现内核行为”）。
7. **机器学习调度**：用模拟器生成大规模数据，训练模型预测 `Speedup_Benefit`，
   替代手写 CRP 函数（呼应初赛文档中“机器学习集成”的设想）。

---

## 5. 建议的执行顺序

```
P0 解冲突 ─► P1 文档/构建/CI ─► P2 内核质量(A4-A7)＆模拟器(B1-B4)
                                        │
                                        └─► P3 选择一个研究深化方向(D1-D7)冲刺论文
```

每完成一项，建议同步更新：`docs/ARCHITECTURE.md`、`docs/CONFLICT_ANALYSIS.md`（若涉及）、`CHANGELOG.md`。
