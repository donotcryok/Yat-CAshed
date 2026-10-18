# Yat-CAShed 改进与开发方向（Roadmap）

> 本文给出从「当前状态」到「工程化、可复现、可发表」的几个明确方向，按优先级与投入分层。
> 其中 **P0** 为阻断性问题，必须先解决才能继续。

> 📌 **三份文档的分工**
> - [`PROJECT_AUDIT.md`](PROJECT_AUDIT.md) —— **现状与问题清单**（B/D/K/S/R 编号，可复现命令与数字）；
> - `ROADMAP.md`（本文）—— **任务级拆解**：A1…D、P0…P3、预估投入、验收标准；
> - [`DIRECTIONS.md`](DIRECTIONS.md) —— **方向级判断**：每条路线的取舍理由与「不建议做的事」。
>
> 若三者出现冲突，以本文的验收标准为准，并回改另两份。

---

## 0. 优先级总览

| 层级 | 方向 | 状态 | 剩余工作 |
|------|------|------|---------|
| ~~**P0**~~ | ~~解决 `yat_casched.c` 合并冲突~~ | ✅ **已完成（A1）** | — |
| **P1** | 文档与目录工程化（方向 C） | ✅ 门禁/CHANGELOG/CONTRIBUTING/CI 已完成 | 命名统一、两份「完整实现文档」归档、LICENSE 澄清 |
| **P1** | 构建与 CI | ✅ **已完成（A2 / B1 / B2）** | — |
| **P2** | 内核模块质量（A4–A7） | ⏳ 进行中 | A4 脆弱集成点、A5 并发与内存、A6 可观测性、A7 测试体系 |
| **P2** | 模拟器工程化（B3–B4） | ✅ 主体完成 | B3 剩余：参数外置、CSV schema 文档 |
| **P3** | 算法与研究深化（D1'–D9'） | ⏳ 待启动 | **先修度量**：D1' 真实代价模型 → D2' trace 负载 → D8' 仿真↔内核一致性 |

---

## 1. 方向 A：内核调度器（`linux-6.8-main/`）

### A1. 恢复可编译（P0）✅ 已完成（含真实编译验证 + 仓库完整性修复）
已按 **路线 A（决赛 AJLR 版）** 解决 10 处冲突：文件由 1791 行重建为约 1396 行、0 标记，
并移除 HEAD 侧残留的 v2.2 全局调度逻辑与重复符号。

**真实内核编译已验证通过**：
`kernel/sched/yat_casched.o` = 14136 字节、0 error / 0 warning；
`vmlinux` = 11856568 字节、0 error / 0 warning；`System.map` 含 33 个 `yat_*` 符号。
前置条件：`CONFIG_SMP=y`（否则 `sched_class` 的 `balance`/`select_task_rq`/`pick_task`
等成员与 `rq::cpu` 不存在）。

**仓库完整性已修复**（此前只能在 `/tmp` 副本上编译，工作区不可编译）：
- 新增 `.gitattributes`（`* text=auto eol=lf` + 二进制白名单，内核树内的
  二进制例外排在通用规则之后），全树 CRLF→LF 81146 个文件。
- 从上游补齐 1487 个丢失文件（1326 `.S` + 110 `.tc` + 22 `.log` + 其余），
  恢复 1225 个 shebang 脚本的可执行位。
- 修掉 `.gitignore` 四类误伤：`*.s`→`.S`、`*.d`→`test.d`/`conf.d` 目录、
  `tags`→`selftests/arm64/tags/` 目录、`*.log`→kunit `test_data/*.log`。
- **全新 clone 直接 `make vmlinux` 一次通过**（0 error / 0 warning）。
- 第二轮审计再修两类**不报错但语义已错**的损坏：15 个断链 symlink
  （`scripts/dtc/include-prefixes/*` 断链会让 `make dtbs` 必失败）、
  13 组大小写冲突导致的内容错位（如 `xt_dscp.h` 里写的是 `xt_DSCP.h` 的内容）。
  修复后与上游的内容差异仅剩 11 个文件，全部是有意为之的 Yat 改动。

详见 [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §7、§8.4、§8.5、§8.6。

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

### A3. 将调度器产出一份「纯补丁」（P1）✅ 已完成
当前改动**散落在 8+ 个标准内核文件**中，难以评审与回溯 —— 已解决：

- ✅ 产出 [`patches/`](../patches)：**6 个补丁 / 188 KB**，覆盖全部 Yat 改动
  - `0001-sched-yat-casched-core.patch` —— 调度类主体
  - `0002-sched-yat-casched-integration.patch` —— 8 处标准内核集成点
  - `0003-x86-realmode-tolerate-missing-trampoline.patch` —— 编译兼容
  - `0004-kernel-gitignore.patch` / `0005-test-env.patch` / `0006-docs.patch`
  - [`series`](../patches/series)（`git quiltimport` 可直接消费）+
    [`MANIFEST.sha256`](../patches/MANIFEST.sha256)
- ✅ [`scripts/gen_kernel_patches.sh`](../scripts/gen_kernel_patches.sh) 生成并**断言往返一致**
- ✅ [`scripts/kernel-patch.sh --selftest`](../scripts/kernel-patch.sh)：
  apply 后与仓库逐字节相同；reverse 后与上游逐字节相同
- ✅ `make patch-check` 接入门禁
- ✅ 效果：1.6 GB / ~83k 文件的 `linux-6.8-main/` **不再是携带 Yat 改动的必需物**
- ⏳ 未做：进一步减少对 `core.c` / `sched.h` 的侵入（需重新设计调度类挂载方式）

### A4. 修复脆弱的内核集成点（P2）
- `include/asm-generic/vmlinux.lds.h:135` 手工插入 `__yat_casched_sched_class` 段，
  顺序错误会被 `core.c:9951` 的 `BUG_ON` 直接 panic。建议加注释与编译期断言说明约束。
- `kernel/sched/sched.h` 重复 include（已修一处），建议通查。
- ~~`init/Kconfig` 的配置项**缩进用了空格**而非 Tab~~ ✅ **已改为 Tab**。
  仍待办：位置不在 `kernel/sched/Kconfig`，风格上仍不符内核惯例。

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

### B3. 可复现实验（P2）✅ 已完成（含公平基线 C1）
- ✅ 随机种子已显式化并可通过 `--seed=` 指定；默认种子 42。
- ✅ 实验参数已外置为命令行参数：`--cores= / --cases= / --tasks= / --seed= / --seeds= / --no-viz / --legacy-metrics`，
  并修复了用例在利用率等级间分布不均的整数除法缺陷（见 [`AlgorithmComparisonExperiment.java`](../simulator/AlgorithmComparisonExperiment.java)）。
- ✅ **多种子支持**：`--seeds=42,43,44,45,46`（或 `--seeds=5`），输出均值 / 样本 SD / 95% CI / 逐种子取值。
- ✅ **公平基线（C1）已落地**：WFD 与 CacheAware_v2 共用 [`SharedCacheModel`](../simulator/entity/SharedCacheModel.java)，
  命中率改为实测值，`expectedET` 不可被算法改写。旧口径可用 `--legacy-metrics` 逐位复现。
- ✅ **公平口径全量结果**（seed=42，100 用例 / 60 任务）：
  Makespan 176,436.8 vs 177,686.2 ns（配对均值 **+0.39%**，SD 7.70%，**CI ±1.51% 跨 0**）；
  命中率 18.90% vs 18.08%（**+0.82 pp**）；胜场 **56/44**。
  5 种子复核（n=5，95% CI 用 t(0.975,4)=2.776）：Makespan **−0.954% ± 1.745%**、
  命中率 +0.377 pp ± 0.338、胜率 48.6% ± 7.165%。
- ✅ 同时修复了 Python 解释器选择缺陷、matplotlib ≥3.9 `boxplot(labels=)` 移除导致的图表生成失败，
  以及 `ResultVisualizer` 改进方向符号反转（曾误报「Makespan 增加 13.53%」）的缺陷。
- ✅ **实验报告已落地**：[EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md)（设计 / 参数 / 公平性审计 §3.6 /
  总体与分利用率统计 / 极端用例 / 机制解释 §4.5 / 有效性威胁 / 复现命令）。
- ⏳ 待办：把参数外置为 `config/*.yaml`；统一 CSV schema 与图表命名文档。

### B4. 流水线整合（P2）✅ 已完成
- ✅ `run` 目标中的 Windows 命令已消除，改为跨平台实现。
- ✅ **零依赖单元测试** [`simulator/tests/YatSimulatorTest.java`](../simulator/tests/YatSimulatorTest.java)（**72 断言**），
  覆盖 Node 约束、CRP 的 L1/L2/L3/内存映射及**边界条件**（同时命中、跨 DAG、访问窗口、
  故障叠加、`(long)` 截断）、WFD 与 CacheAware_v2 契约、Speedup/Benefit 的阈值与对称性，
  以及 `[6]` **公平性断言**（`expectedET` 不被改写 / 层级决策确定性 / 命中率与算法名无关）。
- ✅ CI（[`.github/workflows/ci.yml`](../.github/workflows/ci.yml)）只跑 `compile → lib-test → test → run-quick`，
  可视化（依赖 pip）不进 CI；可视化调用可视环境自动降级。
- ✅ `requirements.txt` 已就位，`docs/DEVELOPMENT.md` 补充了 Python 依赖说明。

---

## 3. 方向 C：文档与仓库治理（P1）

- **统一命名**：确定唯一项目名（建议 `Yat-Casched`），全仓库统一，避免 `Yat_Casched`/`Yat_CAShed`/`YAT_CASCHED` 混用。
- **单一事实来源**：两份“完整实现文档”合并为一份（按冲突路线的最终选择更新），另一份标注 `[已归档]` 并移至 `docs/archive/`。
- ✅ **变更日志**：[`CHANGELOG.md`](../CHANGELOG.md) 已就位（Keep a Changelog 风格，含 `Unreleased` 段与「已知遗留」）。
- **LICENSE**：内核部分 GPL-2.0，模拟器/文档需明确授权（当前 `README` 只写了“学术使用权以参考论文为准”，法律上不清晰）。
- ✅ **CONTRIBUTING.md**：[`CONTRIBUTING.md`](../CONTRIBUTING.md) 已含分支模型、`feat:/fix:/chore:` 提交规范、提交前检查清单、文档同步矩阵。
- ✅ **CI**：[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) 已覆盖 ①冲突标记 ②模拟器编译与回放 ③**文档链接检查**（`scripts/check_docs.py`）。
- ✅ **文档门禁**：`make docs-check`（25 文件 / 261 链接 / 0 失效），与 `make check`、`make symbol-check`
  一并由 `make verify-all` 串联。
- ⬜ **仍待处理**：项目名统一（`Yat-Casched` vs `Yat_CAShed` 仍混用）、两份“完整实现文档”的归档收敛。

> 方向 C 的具体取舍（为什么先做门禁、为什么不立刻重写历史）见 [`DIRECTIONS.md`](DIRECTIONS.md)。

---

## 4. 方向 D：算法与研究深化（P3）

### 排序依据（C1 之后的修正）

C1 公平基线跑完后，模拟器的结论从「CacheAware 领先 11.66%」落到「配对均值 0.39%、
95% CI 跨 0」。机制审计（[EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md) §4.5）指出根因：
**60 个任务里有 40 个在公平模型下落到 L3（代价系数 0.95），收益天花板本身就只有约 5%**，
再被忙核上的排队延迟抵消。

因此 D 类方向不能再按「哪个听起来更前沿」排序，而要按
**「先修好度量，还是先调参」** 排序：模型保真度没解决之前，任何参数调整都无法证伪。

### D1'. 真实硬件代价模型（**当前最高优先级**）
- 现状：代价系数是手工常量（L1 0.8 / L2 0.9 / L3 0.95 / mem 1.2，下限 `base/2`），
  层级由 `SharedCacheModel` 的**写死规则**决定（同 DAG 且 `processor/2` 相等才升 L2）。
- 目标：用 `perf stat` / `rdpmc` 采集真实 `cycles`、`LLC-load-misses`、`stalled-cycles-*`，
  拟合出「迁移距离 → 实际执行时间」的函数，替换常量表。
- 为什么必须先做：现有天花板（≈5%）来自**模型假设**而非算法，若不做这层校准，
  D3'–D9' 的任何增益都无法与建模噪声区分。
- 前置条件：D8'（trace 一致性）的输入管线可复用。

### D2'. trace 驱动的负载保真
- 现状：`EnhancedTaskGenerator` 用 `UUnifastDiscard` 合成 DAG，参数（周期、层数、
  缓存权重的分布）全是手写分布。
- 目标：从真实 trace（PARSEC / SPLASH-2 / 内部采集）提取 DAG 结构与访问序列，
  直接驱动模拟器。
- 价值：C1 已暴露「合成负载的权重分布 [0.5, 0.3, 0.15, 0.05] 决定了几乎全部结论」，
  换成 trace 才能谈「在什么负载下有效」。

### D3'. 自适应缓存热度窗口（原 D1）
初赛版固定 10ms。可基于 workload 特征（迁移频率、cache 命中反馈）在线调整窗口，
形成「自适应 AJLR」。
- 前置：需要 D1' 的真实代价信号作为反馈量，否则调整目标仍是模型内部的假量。

### D4'. CRP 模型数据驱动化（原 D2）
当前 `get_crp_ratio()` 是手写分段函数（桩）。可改用**离线 profile 拟合的真实 CRP 曲线**
（论文 RTNS'23 的方法），显著提升说服力。
- 前置：与 D2' 共用 trace 采集。

### D5'. NUMA 感知扩展（原 D3）
`calculate_recency()` 已区分 L1/L2/L3，可进一步引入 NUMA 距离矩阵，
把“缓存亲和”升级为“缓存 + 内存带宽亲和”。

### D6'. 与 EAS / schedutil 协同（原 D4）
把缓存感知收益纳入频率/能耗决策，做能效（perf/Watt）对比。
- 注意：模拟器现有能耗模型是「CPU 利用率 × 常量」的估算式
  （[EXPERIMENT_REPORT.md](EXPERIMENT_REPORT.md) §4.4），做 perf/Watt 之前须先换掉它。

### D7'. 实时性分析（原 D5）
项目资料含大量 DAG/实时调度理论（`chapter2/4`）。可对 AJLR 版做**可调度性分析**
（response-time / makespan bound），而不止实验对比。

### D8'. 模拟器 ↔ 内核一致性验证（原 D6）
让 Java 模拟器与内核调度器在**同一组 trace 输入**下产出可对比的决策序列，
这是很强的论文贡献点（“仿真可复现内核行为”）。
- 这一项同时是 D1'/D2' 的技术底座，建议与它们一起推进。

### D9'. 机器学习调度（原 D7）
用模拟器生成大规模数据，训练模型预测 `Speedup_Benefit`，替代手写 CRP 函数
（呼应初赛文档中“机器学习集成”的设想）。
- 前置：D1'–D2' 完成。否则训练标签本身就是错误模型的产物，学到的只是模型偏差。

---

## 5. 建议的执行顺序

```
P0 解冲突 ─► P1 文档/构建/CI ─► P2 内核质量(A4-A7)＆模拟器(B3 剩余)
                                        │
                                        └─► P3 先修度量(D1' D2' D8') ─► 再调算法(D3'-D9')
```

当前进度：**P0 ✅、P1 ✅**（文档门禁 / 跨平台构建 / 依赖治理 / CI 全部落地），
**P2 进行中**（内核并发与内存安全 A5 / 可观测性 A6 / 测试体系 A7，以及 B3 的参数外置是下一批重点），
**P3 待选**：C1 已证明「当前模型下平衡点的收益天花板约 5%」，
因此建议顺序是 **D1'（真实硬件代价模型）→ D2'（trace 负载）→ D8'（仿真↔内核一致性）**，
把度量修好之后再启动 D3'–D9' 的参数/算法类方向。

**按投入排序的下一步**（都不依赖 D 类前置工作，可立即开工）：

| 顺序 | 项 | 预估 | 交付物 |
|------|----|------|--------|
| 1 | A5 并发与内存安全审查 | 2–3 天 | `lockdep` + KASAN 一轮报告、加锁顺序文档 |
| 2 | A6 可观测性 | 1–2 天 | tracepoint + `debugfs` 节点，去掉调度路径 `printk` |
| 3 | C 命名统一 + 两份「完整实现文档」归档 | 0.5 天 | 全仓库唯一项目名、`docs/archive/` 收敛 |
| 4 | B3 剩余（参数外置 + CSV schema 文档） | 1 天 | `config/*.yaml`、统一图表命名 |
| 5 | A7 测试体系 | 2–3 天 | CFS vs Yat 自动对比脚本 + CSV |
| 6 | D1' 真实硬件代价模型 | 1–2 周 | 由 `perf` 拟合的迁移代价函数 |

每完成一项，建议同步更新：`docs/ARCHITECTURE.md`、`docs/CONFLICT_ANALYSIS.md`（若涉及）、`CHANGELOG.md`，
并跑一次 `make verify-all`。
