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
| **P2** | 内核模块质量（A4–A8） | ✅ 已完成 | A4–A7 已完成；A8 已接线并**实跑通过**（接管率 98%，见本文 §A8.1） |
| **P2** | 模拟器工程化（B3–B4） | ✅ 已完成 | B3 参数外置与 CSV schema 文档已收口 |
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

### A4. 修复脆弱的内核集成点（P2）✅ 已完成
- ✅ `include/asm-generic/vmlinux.lds.h` 的 `YAT_CASCHED_SCHED_CLASS` 已改为**条件编译宏**，
  定位于 `__fair` 与 `__idle` 之间；段名顺序不再依赖人工插入位置。
- ✅ `sched_init()` 加入**可诊断 panic**：一旦链接器把该类排到 `fair` 之后/`idle` 之前的
  位置之外的任何地方，启动即报出具体错位方向与「去改 vmlinux.lds.h」的提示，
  取代原先只有 `BUG_ON` 行号的盲猜。
- ✅ `kernel/sched/sched.h` 重复 include 已通查去重。
- ✅ `init/Kconfig` 配置项缩进已改为 Tab。
- ⚠️ **事实性修正**：原条目写「位置不在 `kernel/sched/Kconfig`，风格不符内核惯例」——
  **Linux 6.8 根本不存在 `kernel/sched/Kconfig`**。调度相关选项本就分散在
  `init/Kconfig`（`SCHED_*` 通用项）与 `kernel/Kconfig.preempt`（抢占模型）中，
  因此放在 `init/Kconfig` 是**符合上游现状**的做法，该项已撤销。

### A5. 并发与内存安全审查（P2）✅ 已完成（静态审查 + 运行时验证，并修掉一处真实死锁）
逐条处置结果（详见 [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §8.7）：
- ✅ **加锁顺序**：文件头登记唯一合法层级 `rq_lock → global_dag_lock → dag->lock → pool->lock`；
  history 表锁与 accel 表锁为**不嵌套的兄弟**，方向不可反转（否则 ABBA）。
  原代码中 `yat_pool_lock` 实际保护的是与 rq 无关的就绪池，属于「用错锁保护数据」，
  已删除该遗留全局锁，改为 `global_ready_pool->lock` 自洽。
- ✅ **`balance` 只在 CPU0**：改为任一 CPU 均可执行，用 `spin_trylock(&yat_balance_lock)`
  保证单飞；避免 CPU0 被长任务占满时全局调度彻底停摆。
- ✅ **原子上下文分配**：`__yat_create_dag()` / `__yat_create_job()` 的 `gfp` 由调用者显式传入；
  `yat_ajlr_enqueue_task()` 这条持 `rq_lock` 的路径用 `GFP_ATOMIC`，boot/测试路径用 `GFP_KERNEL`。
- ✅ **`add_history_record()` 的 GFP_ATOMIC**：保留 `GFP_ATOMIC`（调度热路径），
  但增加软上限 512 条 + 超限裁剪至 3/4，防止无界增长；kfree 移到锁外。
- ✅ **`yat_accel_table_insert()`**：改为「先 `GFP_ATOMIC` kmalloc、后取锁、EEXIST 时释放己方副本」；
  `yat_find_best_core_for_job()` 改为**单遍** `for_each_online_cpu`，只在胜出核心写一次遥测，
  消除「每核一次 kmalloc」。
- ✅ **TOCTOU**：`yat_ready_pool_peek()` 已删除，`yat_schedule_ajlr()` 改为原子
  `yat_ready_pool_dequeue()`，找不到核心时原样 enqueue 放回。
- ✅ **UAF**：`yat_destroy_job()` 先 `yat_ready_pool_remove()` 再 `kfree()`。
- ✅ **初始化竞态**：全局结构拆出 `yat_global_init_once()` + `WRITE_ONCE/READ_ONCE` 保护，
  修复了 `global_default_dag` 从未赋值（AJLR 路径完全失效）的功能回归。
- ✅ **记账正确性**：`update_curr_yat_casched()` 补 `se.sum_exec_runtime`（否则 `task_tick`
  永不触发抢占）；`put_prev_task` 先算 `delta_exec` 再 `update_curr`（否则恒为 0）；
  删除两处 0 长度/wcet 冒充执行时间的错误记账。
- ✅ **依赖图**：`yat_job_add_dependency()` / `yat_job_complete()` 由空 TODO 补全为真实双向挂链
  与幂等完成传播。
- ✅ **运行时验证（已完成）**：用 `defconfig` + `SCHED_CLASS_YAT_CASCHED` + `PROVE_LOCKING` +
  `DEBUG_ATOMIC_SLEEP` + `DEBUG_SPINLOCK` + `DEBUG_LIST` + `DEBUG_OBJECTS` + `KASAN` +
  `DEBUG_KMEMLEAK` 完成全量构建（`arch/x86/boot/bzImage is ready`，编译期零 error），
  并在 QEMU 中实跑（4 vCPU / 1 GB / `-accel tcg`）。

- ✅ **运行时暴露的真实缺陷（本轮最关键的发现）**：
  `debugfs` 的 `yat_ready_pool_show()` / `yat_dags_show()` 用普通 `spin_lock()` 读取
  `pool->lock` / `global_dag_lock` / `dag->lock`，而这三把锁在调度热路径上
  **总是在关中断状态下获取**（`balance_yat_casched()` 运行在 irq-safe 的 `rq_lock` 之内）。
  lockdep 实跑报出：

  ```
  WARNING: possible irq lock inversion dependency detected
   (&pool->lock#3){+...}-{2:2}, at: yat_ready_pool_show+0x32
  but this lock was taken by another, HARDIRQ-safe lock in the past:
   (&rq->__lock){-.-.}-{2:2}
  *** DEADLOCK ***
  ```

  这是一个**真实的 ABBA 死锁窗口**（进程持 `pool->lock` + 中断里 `scheduler_tick()`
  取 `rq->lock` 再取 `pool->lock`），不是 lockdep 误报。
  修复：三个 `_show` 函数全部改为 `spin_lock_irqsave()` / `spin_unlock_irqrestore()`，
  并在 `yat_dags_show()` 内层用 `spin_lock_nested(&dag->lock, 1)` 显式声明
  `global_dag_lock → dag->lock` 这一合法父子层级。
  修复后重跑，两份 QEMU 日志中 `possible irq lock inversion` 计数均为 **0**。

- ✅ **KASAN / Kmemleak**：同一轮实跑中未见 `KASAN:` 报告，未见 `BUG:` / `Oops:` /
  `general protection fault` / `possible circular locking`。

### A6. 可观测性（P2）✅ 已完成
目标：让「调度器内部到底发生了什么」可以被外部观测，而不是靠加 `printk` 刷屏。

**已落地内容**（`linux-6.8-main/kernel/sched/yat_casched.c`）：

1. **6 个 tracepoint**（定义在 `include/trace/events/yat_casched.h`，已接入 `include/trace/events/Makefile`）：
   - `yat_casched_enqueue` / `yat_casched_dequeue`：进出调度类的任务与优先级；
   - `yat_casched_pick`：选中了哪个任务、它的 `job_id`、当前 `nr_running`；
   - `yat_casched_place`：**算法内部**评估出把作业放到哪个核心、依据什么；
   - `yat_casched_wakeup_place`：**内核实际采纳**的选核结果
     （`pid job_id prev_cpu target_cpu prio` + `(stay)` / `(migrate)`）；
   - `yat_casched_account`：记账事件（执行时间统计）。
   用 tracepoint 而非 `printk` 的关键原因：**零成本关闭**（未启用时只是静态分支），
   不会像 `printk` 那样扰动调度时序。

   > `_place` 与 `_wakeup_place` 必须成对看：两者不一致说明决策被亲和性校验
   > 或 `select_fallback_rq()` 推翻。只看 `_place` 会高估 AJLR 的实际影响力。

2. **4 个 debugfs 节点**（`/sys/kernel/debug/yat_casched/`）：
   - `stats`：per-CPU 计数器求和（A8 后扩到 **27 项**，按决策链路分组）——
     负载均衡 `balance_runs/balance_skips/jobs_placed/jobs_nocore`；
     历史与加速表 `history_records/accel_inserts/accel_dup`；
     选核回调 `select_calls/select_not_ttwu/select_nojob/select_sticky_idle/
     select_sticky_interval/select_nocore/select_decided`；
     唤醒链路 `ttwu_state_bail/ttwu_runnable/ttwu_wakelist/ttwu_ok`；
     入队出队 `enq_calls/enq_cpus_allowed/deq_calls/deq_sleep`；
     选取 `pick_calls/pick_null_desync`；
     接管率 `place_hit/place_miss/place_pending`；
     外加 `nr_cpu_ids` 便于确认 SMP 是否真的生效。逐项读法见
     [`ARCHITECTURE.md`](ARCHITECTURE.md) §3.1.1；
   - `ready_pool`：就绪池快照（作业 ID、任务 PID、优先级、DAG 优先级、WCET、状态）；
   - `dags`：DAG 列表与每个 DAG 内作业的 waiting / ready / running 分布；
   - `placements`：`job_id pid prio assigned actual verdict` + `ok/denied/pending` 汇总。
     注意它是**实时快照**：任务退出时 job 被 `.task_dead` 释放，workload 结束后必然为空。
     可留存的接管率证据是 `place_hit/place_miss/place_pending` 累计值。

   注册方式：`late_initcall(yat_debugfs_init)` —— debugfs 本身是 `core_initcall`，
   在 `sched_init()` 时还不存在，所以只能在 `late_initcall` 阶段挂节点。
   所有 `_show` 函数按 A5 的结论使用 `spin_lock_irqsave()`。

   计数用 `this_cpu_inc()` / `this_cpu_add()` 累加，读取时用 `per_cpu_ptr` + `offsetof`
   遍历求和，避免读取端引入全局锁竞争。

**验收**：QEMU 实跑后 `cat /sys/kernel/debug/yat_casched/stats` 有真实非零值
（`balance_runs` 在调度类被调用后递增），且该节点本身**不产生 lockdep 警告**。

### A7. 测试体系（P2）✅ 已完成
- ~~`boot_test_scripts` 有**空文件**（`yat_scheduler_test.c`、`test_simple_history_validation.c`）~~ ✅ 已删除。
- ✅ **目录职责统一**（物理隔离，不靠约定）：
  - `boot_test_scripts/` 只放 **C 源码**（`yat_simple_test.c`、`yat_bench.c`）+ 说明文档；
  - `yat_test_scripts/` 只放 **Shell 脚本**，共 9 个（含新增的 `build_yat_simple_test.sh`、
    `start_yat_simple_test.sh`、`compare_cfs_vs_yat.sh`）。
  - `git mv` 迁移了两个脚本；`create_test_initramfs.sh` 的编译产物从
    `$BOOT_DIR/$name` 改为 `test_with_programs_build/$name`，
    使源码目录**物理上**不再出现二进制。
  - `.gitignore` 相应补齐 `test_with_programs_build/`、`yat_test_scripts/results/` 等条目。
- ✅ **新增负载基准** `boot_test_scripts/yat_bench.c`：N 进程 × M 次固定工作量，
  输出机器可解析的单行 `YATCSV,policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,
  max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online,affinity,sleep_us`，
  退出码 `0` 成功 / `2` 调用失败 / `3` 内核无该调度类。
  末尾两列是 A8 追加的**自描述列**（加在行尾，旧列位置不变），用于事后判断
  这组数据能否评价 AJLR。
- ✅ **新增自动对比脚本** `yat_test_scripts/compare_cfs_vs_yat.sh`：
  - 同一份内核镜像、同一份 initramfs、同一份 `yat_bench` 二进制；
  - 通过内核命令行 `yatbench=cfs|yat` 让 guest 侧在启动时切换策略 ——
    **唯一变量是调度策略**；
  - 两轮是**两个独立 QEMU 进程**（不是同一 guest 内先后跑），避免热状态污染；
  - 自动预检（qemu / gcc / cpio / gzip / busybox / 内核镜像 / `CONFIG_SCHED_CLASS_YAT_CASCHED`）；
  - 每轮结束后扫描内核日志中的 `BUG:` / `KASAN:` / `possible circular locking` /
    `Oops:` / `watchdog: BUG` 等模式，命中即判定该轮数据不可信；
  - 自动 dump A6 的 debugfs 三个节点到日志；
  - 产出 `results/cfs_vs_yat.csv` 与配对摘要（比值、上下文切换差）。
- ✅ **Makefile 接线**：新增 `kernel-obj` / `kernel-build` / `kernel-bench` / `kernel-compare`
  四个目标，从仓库根目录即可驱动。

**实测数据（⚠️ 已作废，保留仅为记录）**：

```csv
policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online
cfs,4,2000000,0,228582740,15985290,77244970,8000000,0,55,4
yat,4,2000000,0,61304740,12182720,19416740,8000000,0,7,4
```

这组数据（`yat/cfs ≈ 0.268`）是在 **A8 之前**采集的，当时默认 `--affinity shared`
把每个任务都 pin 到单核且从不阻塞，`select_task_rq` 根本不会调用调度类；
更关键的是 A8 实跑时才发现 yat 类在那个阶段**连一个任务都没有**
（`__normal_prio()` 把 yat 优先级映射进了实时区间，任务被挂到 `rt_sched_class`）。
因此「yat 快 4 倍」既不是 AJLR 的效果，也**不代表 yat 调度类本身**的效果。

当前有效数据见本文 §A8.1：AJLR 接管率 98%，
但墙钟约为 CFS 的 **1.9–2.2 倍**。历史数据的正确读法见
[`CONFLICT_ANALYSIS.md`](CONFLICT_ANALYSIS.md) §8.7.5–§8.7.6。

**必须带上的限定（诚实披露）**：那轮实测的 debugfs 显示
`jobs_placed=0`、`history_records=0`、`accel_inserts=0`，而 `balance_runs` 非零。
当时的原因是 `yat_ajlr_enqueue_task()` 只把 `yat_job` 绑定到 `task_struct`、
**并不插入 `global_ready_pool`**，AJLR 就绪池只被 `yat_create_test_dag()` 演示路径使用。
因此上述时间差**来源于「调度类切换」本身**（短队列选择器 vs CFS 红黑树 + vruntime），
**不是 AJLR 作业分配算法的效果**。

⚠️ **该限定对历史数据继续有效，但对之后的实测不再成立**：A8 已把
`select_task_rq` 接成真正的选核路径（见 A8），此后 `jobs_placed` / `history_records` /
`accel_inserts` 应当非零。**但 A8 之后尚未重跑 `kernel-compare`**，
所以目前**没有任何一组数据**可以声称体现了 AJLR 效果——
在那组数据跑出来之前，本文与新 CHANGELOG 都不填数字。
脚本的 `jobs_placed == 0` 提示保留：它是回归守卫，不是过时逻辑。

**两个实测踩到的引导坑**（已写入脚本注释与文档）：
1. `-kernel` 不能指向 ELF `vmlinux`（`Error loading uncompressed kernel without PVH ELF Note`），
   必须用 `arch/x86/boot/bzImage`；
2. 不能加 `acpi=off` / `noapic` / `nolapic` —— `-kernel bzImage` 绕过 BIOS，
   AP 只能靠 ACPI MADT 枚举，关掉后 guest 退化为 `nr_cpu_ids:1`，对比失去意义；
   保留 ACPI 后 `poweroff -f` 也能真正让 QEMU 退出。

### A8. AJLR 决策落地（P2 续）✅ 已完成（实跑验证：接管率 98%）

**问题**：A6/A7 的所有观测数据都指向同一个尴尬事实——AJLR 算出了放置，
但主调度路径没人采纳它（`jobs_placed` 恒 0）。这属于 [`ARCHITECTURE.md`](ARCHITECTURE.md)
§5.1 登记的 P13。

**本轮改动**（算法本身零改动，只做接线）：

| 环节 | 改动 | 关键理由 |
|------|------|----------|
| 选核 | `select_task_rq_yat_casched()` 接入 `WF_TTWU`，把 `yat_find_best_core_for_job()` 的结果写进 `assigned_cpu` 并**返回给内核** | 这是内核**唯一**合法的跨核选核钩子：包装层在 `p->pi_lock` 下调用它，之后统一 `is_cpu_allowed()` → `set_task_cpu()` → `activate_task()`。因此返回任意 CPU 都安全，亲和性由通用路径兜底 |
| 不在 `balance()` 里做 | 保持原状 | `balance()` 持本核 `rq_lock`，跨核迁移需目标核 `rq_lock`，构成 `rq_lock(A)→rq_lock(B)` 嵌套，违反 A5 锁序 |
| 成本控制 | 目标核 `nr_running == 0` 直接沿用；否则 `YAT_DECISION_MIN_INTERVAL_NS = 1 ms` 内不重算 | `yat_calculate_recency()` 是 O(nr_cpus²)（每个候选核都遍历一遍在线 CPU 的哈希桶），而唤醒是高频事件。不节流会直接把唤醒延迟变成主要开销 |
| 出队 | `pick_next_task_yat_casched()` 两段式：优先选 `assigned_cpu == rq->cpu \|\| INVALID` 的任务，没有才退回纯优先级 | 让 `assigned_cpu` 在**出队侧**也生效，否则「选核建议」会被本地队列的选择器完全抹平 |
| 生命周期 | `.task_dead = task_dead_yat_casched`；`enqueue_task` 改为 `yat_get_or_create_job()` | 原实现每次入队都新建 job 并覆盖指针，旧 job 永不释放（P14）。`task_dead` 在 `finish_task_switch()` 的 `finish_lock_switch()` 之后调用，不持 rq 锁，可安全 `kfree` |
| 可观测量 | 新 tracepoint `_wakeup_place` + 新 debugfs 节点 `placements` | `_place` 是「算法想放哪」，`_wakeup_place` 是「内核实际放哪」。只有后者能算接管率 |

**验收（两档均已完成）**：
- ✅ 编译 / 补丁 / 门禁：`make kernel-obj`、`make patch-selftest`、`make verify-all` 全绿。
- ✅ **实跑**（见下节 A8.1），`jobs_placed > 0`，且能给出可留存的接管率。

#### A8.1 实跑结果与「接线之外还差什么」

第一次真正跑起来时 guest **静默挂死**（串口无输出、四个 vCPU 全部 idle、
`jobs_placed` 恒 0）。逐层加探针（`select_*` / `ttwu_*` / `pick_*` / `deq_*`）
并配合 QEMU gdbstub 读取运行中的 per-CPU 计数器后，定位到**四个致命缺陷**，
全部属于「调度类接入内核」的工程细节，与 AJLR 算法本身无关：

| # | 缺陷 | 后果 | 修复 |
|---|------|------|------|
| 1 | `__normal_prio()` 对 yat 策略返回 `prio = rt_priority`（0） | `prio` 落进实时区间，`__setscheduler_prio()` 的 `rt_prio(prio)` 分支抢先命中 → 任务被挂到 **`rt_sched_class`**，yat 类里一个任务都没有 | 映射到普通区间高端 `MAX_RT_PRIO + rt_priority`（100..119） |
| 2 | `__setscheduler_prio()` 的 yat 分支排在 `rt_prio()` 之后 | 只能靠 prio 区间「碰巧」生效，策略被 prio 覆盖 | 把「按策略判定」提到 `rt_prio()` **之前** |
| 3 | `sched_fork()` 只按 prio 在 RT/CFS 间二选一 | yat 任务 fork 出的子任务被降级成 CFS | 增加 `yat_casched_policy(p->policy)` 分支 |
| 4 | `enqueue/dequeue_task_yat_casched()` **只维护本类私有计数**，未调用 `add_nr_running()/sub_nr_running()` | 本内核中这两个函数由**各调度类自己**调用（fair/rt/dl/stop 均如此）；漏掉后 `rq->nr_running` 恒 0 → `__pick_next_task()` 快路径判定 `rq->nr_running == cfs.h_nr_running == 0`，**根本不遍历 yat 类** → CPU 直接进 idle，任务永久停在 `on_rq` 却永不运行 | enqueue 里 `add_nr_running(rq, 1)`，dequeue 里成对 `sub_nr_running(rq, 1)` |

诊断手段（已固化为可复用的计数器，见 [`ARCHITECTURE.md`](ARCHITECTURE.md) §3.1.1）：
`select_calls/sticky_*/nocore/decided`、`enq_calls/enq_cpus_allowed`、
`ttwu_state_bail/ttwu_runnable/ttwu_wakelist/ttwu_ok`、
`pick_calls/pick_null_desync`、`deq_calls/deq_sleep`、`place_hit/miss/pending`。

**实测数据**（`./compare_cfs_vs_yat.sh --tasks 8 --iters 2000000 --affinity none --sleep-us 200`，
smp=4，kernel #14）：

| 指标 | CFS | Yat-CASched |
|------|-----|-------------|
| `ttwu_ok`（yat 任务的正常唤醒） | 0 | 512（= 8 任务 × 64 次 sleep，完全吻合） |
| `select_calls` / `select_decided` | 0 / 0 | 459 / 254 |
| `jobs_placed` | 0 | **254** |
| `place_hit / miss / pending` | — | **925 / 0 / 14 → 接管率 98%** |
| `pick_null_desync`（核心/本类计数失步） | 0 | 0 |
| `invol_ctx`（非自愿切换） | 161 | 56 |
| `sum_wall_ns`（8 任务总墙钟） | 1.70e9 | 3.30e9（**yat/cfs = 1.94×**） |

**结论必须分开陈述**：
1. **A8 本身成功**：AJLR 的选核决策确实被内核执行了 —— 唤醒次数、决策次数、
   放置次数、命中率四者自洽（512 → 459 → 254 → 925/939）。
2. **当前实现更慢**（1.94×）。原因很清楚，属于已知设计代价而非算法失败：
   `wakeup_preempt_yat_casched()` 无条件 `resched_curr()`、
   `pick_next_task` 每次 O(n) 扫描、`yat_find_best_core_for_job()` 是
   O(nr_cpus²) 历史表扫描，且 TCG 模拟下每次上下文切换的成本被放大。
   这些正是 D1'（真实硬件代价模型）要解决的问题。
3. 因此**依然不得**拿本轮数据声称「AJLR 提升了性能」；能声称的是
   「AJLR 已真实接管选核，且当前实现的开销明显高于 CFS」。

#### A8.2 一次被数据否决的"优化"（负面结果，已回退）

`wakeup_preempt_yat_casched()` 原实现是无条件 `resched_curr()`，看上去像是
"每次唤醒都强制切换"的浪费。于是实现了一版按 `job_priority` 的按需抢占
（同优先级不抢占，仅当前任务不是 yat 类或新任务优先级更高时才 `resched`），
并用 8 任务 / 4 核 / 2M 迭代 / 200µs 唤醒的负载各跑 3 次取中位数：

| 版本 | `sum_wall_ns`（3 次） | 中位数 | `invol_ctx` |
|------|----------------------|--------|-------------|
| 无条件 resched（保留） | 3.769e9 / 4.167e9 / 3.682e9 | **3.769e9** | 71 / 91 / 66 |
| 按需抢占（已回退） | 3.923e9 / 3.989e9 / 4.380e9 | 3.989e9 | 81 / 76 / 80 |

按需抢占**没有更快**（中位数反而高 5.8%），上下文切换次数也没有下降。
原因在 `pick_calls ≈ 950` 这个量级就能看出：它约等于 512 次唤醒 + 436 次入队，
即**每次唤醒本来就需要一次调度**，切换不是"抖动"而是负载本身的性质
（每任务每段计算后主动 sleep）。所以无条件 `resched_curr()` 并不是瓶颈。

该改动已回退，**不保留**。这条记录的价值在于：它把"看起来显然的优化"
用数据排除掉，避免后续重复尝试；同时也说明本负载下的主要开销在别处
（O(nr_cpus²) 的 recency 扫描 + TCG 下被放大的切换成本），归 D1' 处理。

**已知边界**：
- 只覆盖唤醒路径；`fork` / `exec` 直接返回 `task_cpu`（此刻历史表全零，重算是噪声）。
- 1 ms 节流是经验值，需 D1' 的真实代价模型来标定。
- `placements` 表是**实时快照**，任务退出时 job 随之释放，因此 workload 结束后
  表必然为空；可留存证据是 `place_hit/miss/pending` 累计计数器。
- 就绪池（`global_ready_pool`）仍只服务显式 DAG 路径，**这是有意的分工**：
  隐式任务作业若同时被本地队列与全局池决策，会表现为反复迁移的抖动，
  因此 `yat_update_ready_pool()` 显式跳过 `job->dag == yat_get_default_dag()`。

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

- ✅ **统一命名**：规范名定为 **`Yat-CAShed`**（依据外部不可变标识：git remote、CI badge、论文标题、`report/paper/Yat-CAShed.tex`）。
  已清理散文层残留，并新增机械门禁 [`scripts/check_names.py`](../scripts/check_names.py)（`make name-check`）。
  内核代码标识符（`yat_casched`、`SCHED_YAT_CASCHED`）是独立命名空间，不跟显示名。
- ✅ **单一事实来源**：两份“完整实现文档”已 `git mv` 到 [`docs/archive/`](archive/README.md)，
  加归档 banner、正文保留原样（不跟随后续修改）；事实来源收敛到 `yat_casched.c` / `ARCHITECTURE.md` / `CHANGELOG.md`。
- ✅ **变更日志**：[`CHANGELOG.md`](../CHANGELOG.md) 已就位（Keep a Changelog 风格，含 `Unreleased` 段与「已知遗留」）。
- ✅ **LICENSE**：[`LICENSE`](../LICENSE) 已分模块澄清——内核新增/修改文件 GPL-2.0；模拟器与自有文档明确为学术研究与教学使用（保留所有权利）；第三方素材（参考论文 PDF、`simulator/lib/*.jar`）归属各自作者。
- ✅ **CONTRIBUTING.md**：[`CONTRIBUTING.md`](../CONTRIBUTING.md) 已含分支模型、`feat:/fix:/chore:` 提交规范、提交前检查清单、文档同步矩阵。
- ✅ **CI**：[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) 已覆盖 ①冲突标记 ②模拟器编译与回放 ③**文档链接检查**（`scripts/check_docs.py`）。
- ✅ **文档门禁**：`make docs-check`（25 文件 / 261 链接 / 0 失效），与 `make check`、`make symbol-check`
  一并由 `make verify-all` 串联。
- ✅ 方向 C 本轮已全部收口（命名 / 归档 / LICENSE），并留下 `make name-check` 作为长期回归门禁。

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
