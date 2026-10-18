# 更新日志（CHANGELOG）

本项目自本文件引入之日起采用 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 风格记录变更。
日期格式为 `YYYY-MM-DD`。

## [Unreleased] — A8 实跑验证：修掉 4 处致命接线缺陷，接管率 98%

本轮把 A8 从「已接线（实测待跑）」推进到「**实跑通过**」。第一次真正跑 yat 负载时
guest **静默挂死**（串口无输出、4 个 vCPU 全部 idle、`jobs_placed` 恒 0）。
用自建的 per-CPU 探针（`select_*` / `ttwu_*` / `pick_*` / `deq_*`）+ QEMU gdbstub
读取运行态计数器逐层定位，最终发现**四个致命缺陷**，全部是「调度类接入内核」
的工程细节，与 AJLR 算法无关。

### 修复（Fixed）

- **`__normal_prio()`：yat 策略的 `p->prio` 被映射进实时区间**。
  原实现 `prio = rt_priority`，`sched_priority = 0` 时得到 `prio = 0`，
  而 `rt_prio(0)` 为真 → `__setscheduler_prio()` 的 `rt_prio(prio)` 分支抢先命中，
  任务被挂进 `rt_sched_class`，**yat 类里一个任务都没有**。
  改为 `MAX_RT_PRIO + rt_priority`（100..119）：既不与实时区间重叠，
  又保留 0..19 的相对顺序。
- **`__setscheduler_prio()`：yat 分支被 `rt_prio()` 抢先**。
  把「按 `p->policy` 判定」提到 `rt_prio(prio)` 之前，策略成为唯一真相来源。
- **`sched_fork()`：子任务被降级成 CFS**。上游只按 prio 在 RT/CFS 间二选一，
  对自定义的普通调度类是错的；补上 `yat_casched_policy(p->policy)` 分支。
- **`enqueue/dequeue_task_yat_casched()` 漏调 `add_nr_running()/sub_nr_running()`**（最隐蔽）。
  本内核中这两个函数由**各调度类自己**在 enqueue/dequeue 里调用
  （`fair.c`/`rt.c`/`deadline.c`/`stop_task.c` 都显式调用），yat 类只加了自己的
  `yat_rq->nr_running`。后果不是「统计不准」而是**整机静默挂死**：
  `rq->nr_running` 恒 0 → `__pick_next_task()` 快路径判定
  `rq->nr_running == rq->cfs.h_nr_running == 0` → **根本不遍历 yat 类** →
  CPU 直接进 idle，任务永久停在 `on_rq` 却永不运行。
  现在 enqueue 里 `add_nr_running(rq, 1)`、dequeue 里成对 `sub_nr_running(rq, 1)`。

### 新增（Added）

- **`struct yat_casched_stats` 扩容为一套完整链路探针**，debugfs `/stats` 全部可见：
  - `select_calls/not_ttwu/nojob/sticky_idle/sticky_interval/nocore/decided`：选核回调的每个出口；
  - `enq_calls/enq_cpus_allowed`：入队次数与 `nr_cpus_allowed` 之和（证明包装层条件成立）；
  - `ttwu_state_bail/ttwu_runnable/ttwu_wakelist/ttwu_ok`：`try_to_wake_up()` 的四个出口，
    只统计目标是 yat 类的唤醒（由 `yat_probe_ttwu()` 过滤，core.c 在四处出口调用）；
  - `pick_calls/pick_null_desync`：`pick_null_desync` 专门捕获「核心层说有任务、本类说没有」的失步；
  - `deq_calls/deq_sleep`：出队与因阻塞出队的次数；
  - `place_hit/place_miss/place_pending`：**AJLR 接管率**。`placements` 是实时快照，
    任务退出时 job 随之释放，workload 结束后必然为空；这三个累计值才是可留存的证据。
- **`yat_bench` 新增 `--affinity shared|none|spread` 与 `--sleep-us U`**：
  `shared` 会把 `nr_cpus_allowed` 压到 1，`select_task_rq` 直接 `cpumask_any()` 返回，
  **测不到 AJLR**；验证 A8 必须用 `none`（可跨核）+ `--sleep-us`（制造 `WF_TTWU`）。
  另加 `--verbose` 逐阶段打标记，用于挂死时定位。
- **`compare_cfs_vs_yat.sh`**：透传新参数并写入 CSV（追加 `affinity,sleep_us` 两列，
  保持旧列位置不变），运行时自动判定「这组数据能不能谈 AJLR」，并打印 AJLR 接管率。

### 验证（Verified）

`./compare_cfs_vs_yat.sh --tasks 8 --iters 2000000 --affinity none --sleep-us 200`（smp=4）：

| 指标 | CFS | Yat-CASched |
|------|-----|-------------|
| `ttwu_ok` | 0 | 512（= 8 × 64，与负载完全吻合） |
| `select_calls` / `select_decided` | 0 / 0 | 459 / 254 |
| `jobs_placed` | 0 | 254 |
| `place_hit / miss / pending` | — | 925 / 0 / 14 → **接管率 98%** |
| `pick_null_desync` | 0 | 0 |
| `invol_ctx` | 161 | 56 |
| `sum_wall_ns` | 1.70e9 | 3.30e9（yat/cfs = **1.94×**） |

### 已知遗留（Known Issues）

- **性能尚未胜过 CFS**（约 1.9–2.2×）。原因是设计代价而非算法失败：
  `wakeup_preempt_yat_casched()` 无条件 `resched_curr()`、`pick_next_task` 每次 O(n) 扫描、
  `yat_find_best_core_for_job()` 是 O(nr_cpus²) 历史表扫描，且 TCG 放大了切换成本。
  这些正是 D1'（真实硬件代价模型）的输入。
- 本轮数据只能证明「AJLR 已真实接管选核」，**不能**用来声称性能收益。
- **一次被数据否决的优化**：把 `wakeup_preempt_yat_casched()` 从无条件
  `resched_curr()` 改为按 `job_priority` 按需抢占，各跑 3 次取中位数后
  `sum_wall` 反而由 3.769e9 升到 3.989e9（+5.8%），已回退。
  原因：本负载每任务 64 次主动 sleep，每次唤醒本就需要一次调度
  （`pick_calls ≈ 唤醒数 + 入队数`），切换不是抖动。详见 ROADMAP §A8.2。
- 探针计数器长期保留（per-CPU 单指令自增，代价可忽略），便于后续回归时直接读取。

---

## [Unreleased] — 内核：AJLR 决策真正落地（select_task_rq）

本轮修掉的是 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §5.1 里长期登记的
「**AJLR 就绪池未接入主调度路径**」——即 `jobs_placed` / `history_records` /
`accel_inserts` 恒为 0、AJLR 决策只落在 debugfs 数字上而从不出现在真实调度结果里。
**这不是补功能，是接线**：算法本身（CRP 收益、加速表、就绪池）一行未改，
改的是「决策算出来之后有没有人听」。

### 新增（Added）

- **`select_task_rq_yat_casched()` 真正选核**：接入 `WF_TTWU`（`try_to_wake_up`）路径，
  把 `yat_find_best_core_for_job()` 的结果写到 `job->assigned_cpu` 并**作为返回值返回**。
  这是内核**唯一**允许跨核迁移的合法钩子：`core.c` 的 `select_task_rq` 包装层在
  `p->pi_lock` 下先调用本回调，再统一做 `is_cpu_allowed()` 校验 →
  `set_cpu_allowed` 失败则 `select_fallback_rq()` 兜底 → `set_task_cpu()` →
  `activate_task()`。因此本函数**可以返回任意 CPU**，亲和性由通用路径保证，
  不需要也无法在这里自己拿目标 rq 的锁（`balance()` 里做跨核迁移会违反 A5 锁序）。
- **`struct yat_job.assigned_cpu` 成为真实控制信号**（此前只是事后记录的遥测字段）。
- **`struct yat_job.last_decision_ns` + `YAT_DECISION_MIN_INTERVAL_NS`（1 ms）决策节流**：
  `yat_find_best_core_for_job()` → `yat_calculate_recency()` 是 O(nr_cpus²)
  （每个候选核都 `for_each_online_cpu` 走一遍哈希桶），而 `select_task_rq` 每次唤醒
  都会被调用。因此采用「**粘性 + 最小间隔**」：目标核 `nr_running == 0` 时直接留在原核
  （唤醒即跑，重算只会更差），否则最密 1 ms 才重算一次。无锁保护——`assigned_cpu`
  与 `last_decision_ns` 都只在持 `p->pi_lock` 的同一路径上写，读端用 `READ_ONCE`。
- **`yat_get_or_create_job()`**：统一「从 task_struct 取作业，没有就按默认 DAG 建一个」
  这一模式，`enqueue_task` / `select_task_rq` / `prio_changed` 共用，WCET 统一为
  `YAT_DEFAULT_WCET_NS`。
- **`.task_dead` 回调（`task_dead_yat_casched`）**：任务退出时释放其 `yat_job`。
  `task_dead` 在 `finish_task_switch()` 里、`finish_lock_switch()` 之后调用，
  此时 `task_struct` 仍有效且**不持 rq 锁**，因此可以安全 `kfree`；
  `schedule()` 的原子上下文不允许睡眠，但本路径不需要睡眠。
- **`pick_next_task_yat_casched()` 两段式选择**：优先在「`assigned_cpu` 等于本核
  或未定」的任务里按 `job_priority` 选；若一个都没有，则退回纯优先级扫描。
  扫描上限 `YAT_PICK_SCAN_LIMIT = 8`（短队列选择器，不做全链表遍历）。
  这样 `assigned_cpu` 才在**出队侧**也生效，不再只是「选核建议」。
- **新 tracepoint `yat_casched:yat_casched_wakeup_place`**
  （`pid job_id prev_cpu target_cpu prio` + `(stay)` / `(migrate)` 后缀）。
  与既有 `yat_casched_place` 的区别：后者是**算法内部**的评估结果，
  前者是**内核实际采纳**的决策；两者不等即说明决策被亲和性/回落逻辑推翻。
- **新 debugfs 节点 `/sys/kernel/debug/yat_casched/placements`**：
  下表 `job_id pid prio assigned actual verdict`，`verdict ∈ {ok, pending, denied}`，
  末行汇总 `ok / denied / pending` 计数。这是**唯一**能直接度量
  「AJLR 到底管住了多少任务的放置」的接口——`jobs_placed` 只统计「算过」，
  不统计「算完有没有被采纳」。

### 变更（Changed）

- `prio_changed_yat_casched()` 同步 `job->job_priority`（此前只在入队时写一次，
  `sched_setscheduler()` 改优先级后选核用的还是旧值）。
- `yat_schedule_ajlr()` 记录 `job->assigned_cpu = best_core`，并修正一处**永假分支**：
  原判据 `best_core == raw_smp_processor_id()` 恒成立（上一次赋值就是它），
  现改为判断「该作业的任务当前就在本核」，才触发 `resched_curr()`。
- `yat_update_ready_pool()` 跳过隐式任务作业（`job->dag == yat_get_default_dag()`）：
  这类作业由 `enqueue_task` 隐式创建、生命周期跟 `task_struct` 绑定，
  若也走就绪池扫描会导致同一作业被「本地队列」与「全局池」重复决策，
  表现为反复迁移的抖动。显式 DAG（`yat_create_test_dag()` 路径）语义不变。

### 修复（Fixed）

- **唤醒路径的作业泄漏**：原 `yat_ajlr_enqueue_task()` 每次入队都会
  `__yat_create_job()` 建新作业并覆盖 `p->yat_casched.job`，旧作业既不释放也不复用；
  任务睡眠/唤醒若干轮后，`dag->total_jobs` 与实际任务数彻底脱钩。
  现改为 `yat_get_or_create_job()` 复用，作业生命周期与任务一致，
  并由 `.task_dead` 精确释放一次。
- **`enqueue_task` 在 `GFP_ATOMIC` 下的每次分配**：同一处修复顺带消除了
  每个唤醒周期一次原子上下文 `kmalloc`。

### 验证（Verified）

- `make kernel-obj`：exit 0，`CC kernel/sched/yat_casched.o` 无 error / warning。
- `bash scripts/gen_kernel_patches.sh --upstream /tmp/uprev`：6 个补丁，264 K，
  hunk 数 3 / 27 / 1 / 3 / 23 / 1；`patches/MANIFEST.sha256` 已刷新。
- `make patch-selftest UPSTREAM=/tmp/uprev`：正向逐字节一致、反向回到上游一致 →
  **SELFTEST PASS**。
- `make verify-all`：仓库健康 5/5、docs-check 27 篇 / 328 链接 / 0 失效、
  name-check 0 违规、symbol-check 通过、simulator-verify 通过 → **全部门禁通过**。
- `scripts/check_names.py`：为本文件枚举错误拼法的三处（docstring 两行 +
  `VARIANTS` 元组）补 `name-check:ignore` 行级豁免。这是脚本自身文档化的豁免通道，
  **没有放宽门禁规则**——规则必须能逐字写出被禁写法，否则认不出它们。

### 已知遗留（Known Issues）

- **QEMU 实测尚未重跑**：本轮只做到编译 + 补丁 + 门禁。`/placements` 的
  `ok/denied/pending` 比例、以及 `yat_casched_wakeup_place` 的 `(stay)/(migrate)`
  分布，需要在 `make kernel-compare` 里实测后才能给出「AJLR 实际接管率」。
  CHANGELOG 不预填未跑出的数字。
- `select_task_rq` 只覆盖唤醒路径；`fork` / `exec` / 周期唤醒的迁移仍由
  `load_balance` 决定（`WF_FORK` / `WF_EXEC` 直接返回 `task_cpu`，因为此时
  历史表尚未建立，重算是纯噪声）。
- 决策节流 1 ms 是**经验值**，不是推导值；真正的最优值要靠 D1' 的真实代价模型
  与 trace 负载来定。

## [Unreleased] — P1 文档治理收尾 + P2 B3（模拟器参数外置 / CSV schema）

本轮把 P1「文档治理」的最后三块（项目名统一、实现文档归档、LICENSE 授权澄清）
与 P2 B3 的尾巴（参数外置、CSV schema 与图表命名文档）一次收口。
**内核代码逻辑零改动**——内核目录下的变化仅为注释/字符串里的项目名替换，
已由 `make kernel-obj` + `make patch-selftest` 双向验证。

### 新增（Added）

- **参数外置（B3 前半）**：模拟器的实验参数不再硬编码在 Java 里，改为
  `内置默认 < config/*.yaml < 命令行` 三级优先级。
  - [`simulator/parameters/YamlSubset.java`](simulator/parameters/YamlSubset.java)
    —— **零依赖** YAML 子集解析器（约 430 行）。B2 的依赖治理把 `simulator/lib/`
    收敛到单个 JAR，因此不引入 SnakeYAML；改为实现一个**刻意收窄**的子集，
    遇到子集之外的语法（Tab 缩进、行内映射 `{}`、多行标量、锚点/别名、tag）
    一律**报错而不是猜**。所有错误带 `文件:行号`。
  - [`simulator/parameters/ExperimentConfig.java`](simulator/parameters/ExperimentConfig.java)
    —— schema v1（`experiment:` / `system:` 两段，**两段均可选**，段内键严格）。
    刻意拒绝未知键（拼错 `utilisation_levels` 会让实验以旧参数跑完并产出
    一份「看起来正常但标注错误」的 CSV，比直接崩溃危险得多），
    也刻意**不做越界值自动截断**（`high_cache_sensitivity_ratio: 1.5` 直接报错）。
  - [`simulator/config/default.yaml`](simulator/config/default.yaml)
    —— 数值与 B3 之前的硬编码**逐字节相等**，保证无配置文件时行为完全不变。
  - [`simulator/config/README.md`](simulator/config/README.md)
    —— 加载顺序、字段约束表、「不要就地改 default.yaml，要建 profile」政策。
  - `AlgorithmComparisonExperiment` 新增 `--config=<path>` / `--print-config`；
    `parseArgs` 改为**两趟扫描**，使 `--cores=4 --config=a.yaml` 与
    `--config=a.yaml --cores=4` 行为一致（隐性的 flag 顺序依赖是极难排查的 bug 类）。
- **CSV schema 与图表命名（B3 后半）**：
  - [`docs/SCHEMA.md`](docs/SCHEMA.md) —— 模拟器 9 列 CSV 与内核 5 列 CSV 的
    列定义/精度/语义、两侧图表文件命名、变更规则（何时 bump schema 版本）。
  - `ResultVisualizer` 把 CSV 表头提为 `CSV_COLUMNS` 契约常量，
    并新增**类加载期自检**：列数与 `CSV_FORMAT` 占位符数不一致即
    `IllegalStateException`，绝不静默产出错位 CSV。
  - 新增元数据旁文件 `<csv>.meta.json`（`csv_schema_version` / `columns` /
    `algorithms` / `data_rows`）。**不写进 CSV 注释行**的理由：CSV 无标准注释
    语法，`pandas.read_csv` 会把 `#` 行当数据。旁文件也**刻意不含实验参数**，
    那些属于 `config/*.yaml`，重复记录会形成两个会漂移的事实来源。
- `simulator/tests/YatSimulatorTest.java` 新增 `[7] 配置外置` 段（27 条断言），
  覆盖标量类型识别、行内/块状列表等价、Tab 与行内映射拒绝、11 个默认值
  （含数组顺序）、未知键/版本不匹配/越界/长度错误四类负向、CLI 覆盖优先级，
  以及 `applyToSystemParameters` 的**数组拷贝**语义。断言总数 75 → 102。

### 变更（Changed）

- **项目名统一（P1）**：散文层统一为 `Yat-CAShed`；代码标识符保持
  `yat_casched` / `YAT_CASCHED`（内核命名习惯），注册名 `SCHED_YAT_CASCHED` 不变。
  新增 [`scripts/check_names.py`](scripts/check_names.py) 与 `make name-check`，
  机械回归散文层残留（白名单之外出现变体写法即失败）。
- **实现文档归档（P1）**：两份「完整实现文档」用 `git mv` 收敛到
  [`docs/archive/`](docs/archive/README.md)，正文加历史快照 banner、
  不再作为事实来源；全仓相对链接同步修正。
- **LICENSE 授权澄清（P1）**：改为三段式 —— 内核部分 GPL-2.0（与上游一致，
  调度类文件带 SPDX 标识）、模拟器与文档明确「保留所有权利 + 有限学术许可」，
  不再只写「以参考论文为准」这种法律上无法执行的表述。
- **补丁包重生成（A1 回归）**：内核文件的项目名替换改变了
  `0001` / `0002` / `0005` / `0006` 的内容，`patches/` 已重新生成
  （6 个补丁 / 244 KB），`MANIFEST.sha256` 同步刷新。

### 验证（Verified）

- `make verify-all`（check + docs-check + name-check + symbol-check +
  patch-check + simulator-verify）全绿：
  - 27 个 Markdown 文件、321 条相对链接，失效 0；
  - 75 个文件项目名散文层零残留；
  - 符号一致性：头文件声明 28 / 源码定义 26 + 25 static，无缺失、无泄漏、无不一致；
  - `make test`：**102 条断言全通过**（原 75 + 新增 27）。
- `make patch-selftest --upstream <干净 v6.8 树>`：
  正向应用后与仓库**逐字节一致**，反向回退后与上游**逐字节一致** ✅。
- `make kernel-obj`：`CC kernel/sched/yat_casched.o` 通过。
- 配置外置的负向路径逐条实测（均给出可读中文消息、非堆栈）：
  未知键回显键名与可用键列表 / Tab 缩进带 `:3:` 行号 / 缺 `schema_version` /
  `schema_version: 99` / `high_cache_sensitivity_ratio: 1.5`（`必须在 [0,1]`）/
  `--config` 指向不存在的文件。
- CSV 契约：表头仍为原始 9 列（`visualize.py` 按名读取，不受影响）；
  旁文件 `data_rows: 10`、`algorithms: ["CacheAware_v2","WFD"]`。

## [Unreleased] — P2 内核质量：A4 → A5 → A6 → A7

本轮聚焦**真实内核**的工程质量（模拟器只做必要维护）。核心成果是：
把「能编译」推进到「能跑、跑得干净、可观测、可对比」。

### 新增（Added）

- **A6 可观测性**：
  - [`include/trace/events/yat_casched.h`](linux-6.8-main/include/trace/events/yat_casched.h)
    —— 5 个 tracepoint：`yat_casched_enqueue` / `_dequeue` / `_pick` / `_place` / `_account`。
    用 tracepoint 而非 `printk`：未启用时是静态分支，零运行时开销，不会扰动调度时序。
  - `debugfs` 三个节点（`/sys/kernel/debug/yat_casched/`）：
    `stats`（7 项 per-CPU 计数求和 + `nr_cpu_ids`）、`ready_pool`、`dags`。
    注册方式 `late_initcall(yat_debugfs_init)` —— debugfs 是 `core_initcall`，
    `sched_init()` 时还不存在。
  - 7 项 per-CPU 计数器：`balance_runs` / `balance_skips` / `jobs_placed` /
    `jobs_nocore` / `history_records` / `accel_inserts` / `accel_dup`，
    用 `this_cpu_inc()` 写入、`per_cpu_ptr()` + `offsetof()` 求和读取。
- **A7 测试体系**：
  - [`boot_test_scripts/yat_bench.c`](linux-6.8-main/boot_test_scripts/yat_bench.c)
    —— 固定工作量基准（N 进程 × M 迭代），输出单行机器可解析的
    `YATCSV,policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online`。
    退出码 `0` 成功 / `2` 调用失败 / `3` 内核无该调度类。
  - [`yat_test_scripts/compare_cfs_vs_yat.sh`](linux-6.8-main/yat_test_scripts/compare_cfs_vs_yat.sh)
    —— 同一内核、同一 initramfs、同一份基准二进制，通过内核命令行
    `yatbench=cfs|yat` 切换策略；两轮是**两个独立 QEMU 进程**，避免热状态污染。
    自动预检、自动扫描内核日志异常、自动 dump A6 节点、产出
    `results/cfs_vs_yat.csv` 与配对摘要。
  - 顶层 Makefile 新增 `kernel-obj` / `kernel-build` / `kernel-bench` / `kernel-compare`。
- **`.gitignore`**：补齐 `test_with_programs_build/`、`yat_test_scripts/results/`、
  `yat_test_scripts/*.log`、`/boot_test_scripts/yat_bench` 等条目。

### 变更（Changed）

- **A7 目录职责物理隔离**：
  - `boot_test_scripts/` 现在**只放 C 源码**（+ 说明文档）；
  - `yat_test_scripts/` **只放 Shell 脚本**。
  - `build_yat_simple_test.sh` / `start_yat_simple_test.sh` 用 `git mv` 迁到
    `yat_test_scripts/`，并重写为「编译产物落 `yat_simple_test_env/build/`」。
  - `create_test_initramfs.sh` 的编译产物从 `$BOOT_DIR/$name` 改为
    `test_with_programs_build/$name` —— 源码目录**物理上**不再出现二进制。
  - 三份 README（`boot_test_scripts/README.md`、`SIMPLE_TEST_README.md`、
    `yat_test_scripts/README.md`）全部重写，讲清目录约定、CSV 字段、两个引导坑。
- **A4 集成点**：`vmlinux.lds.h` 的 `__yat_casched_sched_class` 段改为 `#ifdef`
  条件编译（未启用时不插入空段）；`core.c` 的调度类顺序校验改为带诊断信息的 panic；
  `sched.h` 重复 `#include` 去除；`init/Kconfig` 位置说明修正（6.8 无
  `kernel/sched/Kconfig`，`SCHED_*` 通用项本就在 `init/Kconfig`）。
- **A5 并发与内存安全**：锁层级文档化 `rq_lock → global_dag_lock → dag->lock → pool->lock`；
  删除误用的 `yat_pool_lock`；`balance` 允许任一 CPU 执行并用 `spin_trylock` 单飞；
  `GFP_ATOMIC` / `GFP_KERNEL` 由调用者显式传入；history 表加 512 条软上限；
  `add_history_record()` 的 kfree 移到锁外；`yat_accel_table_insert()` 改
  「先分配后取锁，EEXIST 释放己方副本」；`yat_find_best_core_for_job()` 改单遍遍历；
  删除 `yat_ready_pool_peek()` 消除 TOCTOU；`yat_destroy_job()` 先摘链再 kfree 消除 UAF。

### 修复（Fixed）

- **真实 IRQ 倒置死锁（由 QEMU 实跑 + lockdep 发现，非理论推演）**：
  `debugfs` 的 `yat_ready_pool_show()` / `yat_dags_show()` 用普通 `spin_lock()`
  读取 `pool->lock` / `global_dag_lock` / `dag->lock`。这三把锁在调度热路径上
  总是在**关中断**状态下获取（`balance_yat_casched()` 运行在 irq-safe 的 `rq_lock` 内），
  因此 lockdep 报 `possible irq lock inversion dependency detected`
  （`&pool->lock#3` vs HARDIRQ-safe 的 `&rq->__lock`，`*** DEADLOCK ***`）。
  修复：三个 `_show` 全部改 `spin_lock_irqsave()` / `spin_unlock_irqrestore()`，
  内层 `dag->lock` 用 `spin_lock_nested(&dag->lock, 1)` 显式声明
  `global_dag_lock → dag->lock` 的合法父子层次。
- **`CHANGELOG.md` 首行历史残留字符** `ao#` → `#`。

### 验证（Verified）

- 单文件编译 `make kernel/sched/yat_casched.o`：零 error / 零 warning。
- 全量构建（`PROVE_LOCKING` + `DEBUG_ATOMIC_SLEEP` + `DEBUG_SPINLOCK` +
  `DEBUG_LIST` + `DEBUG_OBJECTS` + `KASAN` + `DEBUG_KMEMLEAK`）：
  `Kernel: arch/x86/boot/bzImage is ready (#3)`，`grep -cE "error:|Error [0-9]"` = 0。
- QEMU 实跑（4 vCPU / 1 GB / `-accel tcg`）后：
  - `possible irq lock inversion` 计数：cfs 日志 0 / yat 日志 0；
  - `KASAN:` = 0；`BUG:` / `Oops:` / `general protection fault` /
    `possible circular locking` = 0。
- CFS vs Yat 对比（`--iters 2000000 --tasks 4 --cpus 4`，单次）：

  ```csv
  policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online
  cfs,4,2000000,0,228582740,15985290,77244970,8000000,0,55,4
  yat,4,2000000,0,61304740,12182720,19416740,8000000,0,7,4
  ```

  墙钟比 `yat/cfs ≈ 0.268`，非自愿上下文切换 `55 → 7`，
  `sum_work_units` 两轮均为 8000000（= 4 × 2000000）确认负载跑完。
- `create_test_initramfs.sh` 实跑：`boot_test_scripts/` 仍只有 4 个文件
  （2 个 `.c` + 2 个 `.md`），二进制落在 `test_with_programs_build/`。

### 已知遗留（Known Issues）

- **AJLR 就绪池未接入主调度路径**：`yat_ajlr_enqueue_task()` 只把 `yat_job` 绑定到
  `task_struct`，**不插入 `global_ready_pool`**；`pick_next_task_yat_casched()` 是从
  `rq->yat_casched.tasks` 链表里选任务的。因此 debugfs 里 `jobs_placed` /
  `history_records` / `accel_inserts` 常为 0（而 `balance_runs` 非零，证明调度类确实被调用）。
  **这是架构现状，不是 bug**。所以上面的性能数据反映的是「调度类切换」本身
  （短队列选择器 vs CFS 红黑树 + vruntime），**不能作为 AJLR 分配算法有效性的证据**。
  该限定已写入 `ROADMAP.md`、`CONFLICT_ANALYSIS.md §8.7.5`、
  `yat_test_scripts/README.md`，并由脚本在 `jobs_placed == 0` 时自动提示。
- **TCG 单次噪声大**：建议 `--iters` 提到 2000 万以上，或重复多次取中位数，
  不要用单次 `min_wall_ns` 的差异下结论。
- `docs/archive/` 归档收敛、项目名三种写法统一、LICENSE 对模拟器/文档的授权澄清
  仍属 P1 文档治理范畴，本轮未动。

## [Unreleased] — A1 补丁化 + C1 公平基线

### 新增（Added）

- **内核补丁包（A1）**：[`patches/`](patches) 目录，把 Yat 对上游 Linux 6.8 的全部改动
  抽成 **6 个可独立应用的补丁**（合计 188 KB），使仓库不再需要携带 1.6 GB /
  ~83k 文件的 `linux-6.8-main/`。内容：
  - `0001-sched-yat-casched-core.patch` — 调度类主体
    （`yat_casched.c` / `yat_casched.h` / `include/linux/sched.h`）
  - `0002-sched-yat-casched-integration.patch` — 集成点
    （`core.c` / `sched.h` / `init_task.c` / `uapi/sched.h` / `init/Kconfig` / `sched/Makefile`）
  - `0003-x86-realmode-tolerate-missing-trampoline.patch` — 编译兼容修复
  - `0004-kernel-gitignore.patch` / `0005-test-env.patch` / `0006-docs.patch` — 测试环境与文档
  - [`patches/series`](patches/series)（`git quiltimport` 可直接消费）
  - [`patches/MANIFEST.sha256`](patches/MANIFEST.sha256)（逐补丁校验和）
  - [`patches/README.md`](patches/README.md)（应用/反解/校验说明）
- **补丁生成与自检工具**：
  - [`scripts/gen_kernel_patches.sh`](scripts/gen_kernel_patches.sh) — 从工作区与上游
    逐文件 diff 生成补丁，并**断言往返一致**（apply → 与仓库逐字节相同；
    reverse → 与上游逐字节相同）。
  - [`scripts/kernel-patch.sh`](scripts/kernel-patch.sh) — `apply` / `reverse` / `check` /
    `--selftest`；`--selftest` 在临时上游树上完整跑一遍正反两个方向。
  - `make patch-check` 接入门禁。
- **公平缓存执行模型（C1）**：[`simulator/entity/SharedCacheModel.java`](simulator/entity/SharedCacheModel.java)
  （270 行）—— **WFD 与 CacheAware_v2 共用同一个**「缓存层级 + 有效执行时间」决策器：
  - `resolveCacheLevel(task, processor)`：命中 L1（独占核 + 窗口内同 DAG）→ ×0.8；
    L2（同 L2 组，`Level2CoreNum=2`）→ ×0.9；L3（全芯片）→ ×0.95；内存 → ×1.2；
    `hasFaults` 再 ×1.5；下限 `base/2`，至少 1 ns。
  - `execute()` **不改写 `expectedET`**，从结构上保证两算法面对同一任务集。
  - `recordMeasurements()` 把实际层级与命中率写回 `Node.actualCacheHitRatio` /
    `measuredCacheLevel`，供分析器**实测**统计。
- **实测命中率口径**：[`PerformanceAnalyzer.calculateMeasuredCacheHitRatio()`](simulator/analyzer/PerformanceAnalyzer.java#L298)
  优先使用模型写入的实测值，**不再按算法名分派估算公式**。
- **多种子与旧口径开关**（`AlgorithmComparisonExperiment`）：
  - `--seeds=42,43,44,45,46`（或 `--seeds=5`）—— 多种子聚合，输出均值 / 样本 SD /
    95% CI / 逐种子取值。
  - `--legacy-metrics` —— 逐位复现改造前的（不公平）结果，**仅供审计**。
- **公平性单元测试**：[`simulator/tests/YatSimulatorTest.java`](simulator/tests/YatSimulatorTest.java)
  新增 `[6] testFairBaseline()`，14 项断言，覆盖：
  1. 同一任务在两个算法路径下 `expectedET` **不被改写**；
  2. `SharedCacheModel` 对同一 `(task, processor)` 给出**确定性**层级与 ET；
  3. 层级优先级（L1 > L2 > L3 > 内存）与窗口边界（第 5/6 次访问）；
  4. `applyLevelFactor()` 的截断与下限（`base=1`、`base=0`）；
  5. 命中率只取实测值、与算法名无关。
  断言总数 **58 → 72**。
- **公平基线报告**：[`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md) 新增/重写：
  - §1 增加第四个问题 Q4（收益在公平模型下是否成立）；
  - §3.3 两算法共用同一 `SharedCacheModel` 的执行模型伪代码；
  - §3.4 命中率改为「算法无关」定义；
  - §3.5 配对统计与多种子 CI 口径；
  - **§3.6「公平基线改造（C1）」**——4 处根因、6 处修复、改造前后对照表；
  - §4.1 公平总体结果 + 5 种子稳健性表；§4.2 按利用率分解；§4.3 极端用例
    （60/100 例 |Δ|>5%，21/100 例 |Δ|>10%）；
  - **§4.5「为什么收益消失？——机制层面的解释」**——探针实测的层级分布。
- **诊断探针**（临时脚本，不入库）：量化了「CacheAware 确实提升局部性
  （同 DAG 同核 1 → 6 次、L1 命中 0 → 2）但 40/60 任务仍落 L3（×0.95），
  可优化上限仅 5%」这一机制。

### 变更（Changed）

- [`docs/EXPERIMENT_REPORT.md`](docs/EXPERIMENT_REPORT.md)：全文改为**公平基线口径**，
  旧口径数字一律标注为「仅 `--legacy-metrics` 可复现，不可作为算法证据」。
- [`docs/ROADMAP.md`](docs/ROADMAP.md)：D 类研究方向**按 C1 的结论重新排序**。
  - 原 D1–D7 是平铺列表，隐含「先调算法」的假设；C1 测得模型收益天花板仅约 5%
    （40/60 任务落 L3、系数 0.95），意味着**在度量修好前无法证伪任何参数调整**。
  - 重排为 D1'–D9'：新增 **D1' 真实硬件代价模型**、**D2' trace 驱动负载保真**、
    **D8' 仿真↔内核一致性** 三项前置工作，原 D1–D7 顺延为 D3'–D9' 并各自标注前置条件。
  - §0 优先级总览改为「状态 + 剩余工作」两列，§5 增加一张按投入排序的下一步表。
- [`simulator/Simulator.md`](simulator/Simulator.md)：多处**订正声明/实现不一致**
  （A2 延续）：
  - 「📈 性能优化成果」整节重写。删除无来源的
    `1,245,678 ns → 987,654 ns / ↓20.7% / p<0.001`、`命中率 64.2% → 82.5%`、
    `能耗 2,145.6 J → 1,678.3 J` 等数字，改为公平口径实测
    （Makespan 176,436.8 vs 177,686.2 ns；命中率 18.90% vs 18.08%；胜场 56/44；
    CI ±1.51% 跨 0）。
  - 删除**仓库中不存在的**三个"创新机制"伪代码
    （`adjustWeights()` / `predictCacheUtilization()` / `adaptiveWeightLearning()`，
    全仓 grep 0 命中），改为按真实实现
    （[`calculateCacheAwareScore()`](simulator/allocation/CacheAware_v2.java#L104) 四项评分 +
    [`updateProcessorState()`](simulator/allocation/CacheAware_v2.java#L247)）描述。
  - 修正实验配置常量：`TASKS_PER_CASE` 由 50 → **60**，
    利用率级别由 7 级 `{0.4…2.0}` → **5 级 `{0.6,0.8,1.0,1.2,1.5}`**，
    与 [`AlgorithmComparisonExperiment`](simulator/AlgorithmComparisonExperiment.java#L51) 对齐。
  - 修正「预期结果范围」：由「Makespan +15%、命中率 +20%、胜率 70%」改为
    公平口径的可验证性质（共用模型 / 不改写 ET / CI 跨 0）。
  - 「技术特色」表与「算法对比分析」表中的 `+15~25%` / `改善 30%` 一并订正。
- [`docs/ROADMAP.md`](docs/ROADMAP.md)：A3（补丁化）标记为已完成。

### 修复（Fixed）

- **CacheAware 改写 `expectedET`**（公平性根因 1）：`CacheAware_v2.calculateExecutionTime()`
  过去会把增强后的 ET 写回任务，导致 WFD 与 CacheAware 比较的**不是同一任务集**。
  C1 起 ET 由 `SharedCacheModel.execute()` 统一决定，`expectedET` 保持不可变。
- **命中率按算法名分派**（根因 2）：`PerformanceAnalyzer` 曾用两套估算公式，
  且 WFD 侧被 clamp 在 `[0.30, 0.70]`，人为压低基线。改为实测口径。
- **WFD 负载数组硬编码 `int[8]`**（根因 3）：核数 ≠ 8 时统计失真，
  改为按实际核数分配。
- **`createCacheIntensiveTask()` 把 `dagID` 硬编码为 0**（根因 4）：使「同 DAG」判断
  对两个算法都恒为真，亲和性信号失效。改为按 `taskId / DAG_TASKS_PER_GROUP` 计算。
- **`--no-viz` 不导出 CSV**：`exportToCSV` 位于 `generateVisualization()` 内，
  关闭可视化后 `result/algorithm_comparison_results.csv` 会保持旧内容。
  已在文档中明确该行为，避免拿陈旧 CSV 做分析。
- **报告把「百分点」印成「%」**：`generateReport()` 中原为
  `平均缓存命中率改进: %.2f%%`，而该值实际是 `cacheHit 之差`（单位是百分点）。
  改为 `%.2f 个百分点`，避免把 +0.82 pp 误读成 +0.82%。
- **报告未区分「比值口径」与「配对口径」**：`性能改进 - Makespan减少: x ns (y%)`
  中的 `y` 是 `(WFD均值 − CA均值) / WFD均值`，会被大用例主导；而
  `平均Makespan改进` 是逐用例相对改进的均值。现分别标注为
  「比值口径（便于直觉）」与「配对均值（用于显著性检验）」。
- **小样本下仍给出「优势明显/不够稳定」的措辞**：`result 评估` 段在
  `n < 20` 时（如 `make simulator-verify` 的 5 例冒烟）附加一行提示，
  明确该结论仅作冒烟参考，不作为算法结论。
- **文档中的代码行号链接写法失效**：`docs-check` 只校验「路径」部分，
  写成 `Foo.java:298` 会被当作文件名而判为失效链接。已统一改为
  `Foo.java#L298` 的锚点形式（21 处）。
- **多种子 CI 用正态近似替代 t 分布**（偏乐观）：`printAggregate()` 原用
  `1.96 · sd / √n`，n=5 时真值应为 `t(0.975,4) = 2.776`，区间被低估约 29%，
  足以把「不显著」误报为「显著」。现按自由度查表（df ≤ 30），
  并在输出中显式打印所用 `t` 值。更正后 5 种子 Makespan 改进的
  CI 由 ±1.233% 放大到 **±1.746%**（均值 −0.954% 仍跨 0，结论不变，
  但「边际显著」的说法不再成立）。

### 验证（Verified）

- `bash scripts/kernel-patch.sh --selftest` → **往返一致**：
  apply 后与仓库逐字节相同，reverse 后与上游逐字节相同。
- `make patch-check` → 6 个补丁校验和全部匹配。
- `make -C simulator test` → **72/72 断言通过**（新增 14 项公平性断言）。
- **公平基线全量实验**（seed=42，100 用例 × 60 任务）：

  | 指标 | 旧口径 | 公平口径 |
  | --- | --- | --- |
  | Makespan 改进 | 11.66% | **0.39%**（配对均值） |
  | CacheAware Makespan | 155,059 ns | 176,436.8 ns |
  | WFD Makespan | 176,044 ns | 177,686.2 ns |
  | 命中率提升 | +30.21 pp | **+0.82 pp** |
  | CacheAware / WFD 命中率 | 73.8% / 43.6% | 18.90% / 18.08% |
  | CacheAware 胜率 | 96.0% | **56.0%** |
  | 平均响应时间 | — | **+1.47%（CacheAware 更差）** |

  配对差分：均值 +0.39%、中位数 +1.34%、SD 7.70%、**95% CI ±1.51%（跨 0）**；
  |Δ|>5% 的用例 60/100，|Δ|>10% 的用例 21/100。
- **5 种子稳健性**（42–46，n=5，CI 用 t(0.975,4)=2.776）：Makespan 改进 **−0.954% ± 1.745%**、
  命中率 **+0.377 pp ± 0.338 pp**、胜率 **48.6% ± 7.164%** —— 点估计为负且 CI 跨 0。
- `--legacy-metrics --seed=42` 可**逐位复现**改造前的 11.66% / +30.21 pp / 96.0%，
  证明改造是"换口径"而非"改坏了算法"。
- `make verify-all` → 全部门禁通过。

### 已知遗留（Known Issues）

- **公平口径下没有可宣称的算法优势**：这是本次改造**如实暴露**的结果，
  而非回归。历史 11.66% / +30 pp / 96% 主要来自模型校准偏移
  （CacheAware 独占 ET 改写权 + 命中率双套公式）。
- **可优化空间的结构性上限**：缓存层级因子是人为设定（L1 0.8 / L2 0.9 / L3 0.95 /
  内存 1.2），且 60 任务中约 40 个必然落在 L3，理论上限仅 5%。
  要得到有意义的效果，需要换用**真实硬件代价**或**真实 trace**（见 ROADMAP 方向 D）。
- **多种子 n=5 的样本量仍偏小**：CI 已改用 t(0.975,4)=2.776（早期版本误用 1.96，
  已修复）。即便按更宽的区间，Makespan 与胜率的结论不变，但命中率提升
  的区间 [+0.04, +0.72] pp 只是**勉强不含 0**，不应作为独立证据使用。
- **若要获得可宣称的结论，n=5 远远不够**：按当前 SD（SD≈1.4%）估算，
  检出 1% 的 Makespan 差异需要 10 个以上种子；检出 0.5% 需要 40 个以上。
  在投入之前应先确认「差异本身存在」，这需要方向 D 的真实硬件代价模型。
- **`SharedCacheModel` 是模型而非硬件**：所有 "ET 节省" 都是模型参数的直接产物，
  不能当作真实系统的性能证据。

---

## [Unreleased] — 工程化整理

### 新增（Added）

- **文档链接门禁**：[`scripts/check_docs.py`](scripts/check_docs.py) —— 纯标准库实现的
  Markdown 相对链接检查器（跳过代码块/行内代码/外链/锚点/`mailto:`/`data:`，
  处理 `#anchor`、`?query`、URL 编码与 `<...>` 包裹路径）。
  已接入 `make docs-check` 与 CI，当前覆盖 **25 个 Markdown 文件 / 182 条链接 /
  0 条失效**。文档腐化从「靠人肉 review」变为「提交即拦截」。
- **项目审计快照**：[`docs/PROJECT_AUDIT.md`](docs/PROJECT_AUDIT.md) —— 以可复现的
  命令与数字记录仓库现状（2.1 GB 工作区 / 529 MB `.git` / 83568 个受管文件）、
  构建链问题 `B-01..B-06`、文档一致性 `D-01..D-10`、代码与设计 `K-01..K-10`、
  模拟器方法论 `S-01..S-07`、仓库治理 `R-01..R-05`，并给出门禁通过状态与优先级。
- **方向决策文档**：[`docs/DIRECTIONS.md`](docs/DIRECTIONS.md) —— 在 `ROADMAP.md`
  的任务级拆解之上补充**方向级**判断：A 仓库瘦身（推荐补丁化）、B 内核正确性与并发、
  C 实验可信度、D 算法深化，附「不建议做的事」与 M1–M5 里程碑。
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

- **论文构建链修复**（[`Makefile`](Makefile) 的 `report` 目标 + 论文源）：
  - [`report/paper/Yat-CAShed.tex`](report/paper/Yat-CAShed.tex)：`\input{chapters/...}`
    指向**不存在的子目录**（章节文件实际平铺在 `report/paper/` 下），改为平铺文件名。
  - [`report/paper/chapter4-theory.tex`](report/paper/chapter4-theory.tex)：替换 `lmroman10-bold`
    无字形的字符 —— 8 处 `✓`（U+2713）改为 `\checkmark`，1 处 `🎯`（U+1F3AF）删除。
  - `make report` 改为 `latexmk -xelatex -halt-on-error`（无 `latexmk` 时回退两遍 `xelatex`），
    失败即非零退出，不再「构建成功但缺字/缺页」。
  - 清除损坏的 `Yat-CAShed.aux`（含 NUL 字节，导致 `! Text line contains an invalid
    character. l.1 ^^@` 而中断）后重建：**25 页 / 0 missing character**。
- **失效文档链接 4 处**（由新门禁首次运行发现）：两处指向不存在的图片，
  两处 `../` 层级错误；已改为实际存在的 `test_visualization/img/*.png` 与正确相对路径。
- **`create_test_initramfs.sh` 必然失败**：脚本硬编码引用 6 个仓库中并不存在的 `.c`
  测试程序。改为扫描 `boot_test_scripts/*.c` 实际存在的源文件、逐个静态编译、
  按成功列表动态生成 `init`（无源文件时给出明确错误而非静默产出空 initramfs）。
- **文档与实现脱节**：`CONTRIBUTING.md` 引用了未定义的 `make simulator-linux`；
  `docs/ARCHITECTURE.md` / `DIRECTORY.md` 仍称「30+ JAR」「Windows cmd Makefile」
  「`.tex` 为空文件」；三份测试目录 README 描述了已不存在的脚本。
  均已按当前仓库实际内容改写。
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
- **隐性损坏修复**（第二轮审计：与上游逐文件 `filecmp` 比对后发现）：
  - **15 个断链符号链接**恢复为真正的 symlink（git 模式 `100644`→`120000`）：
    `scripts/dummy-tools/{nm,objcopy}`、`scripts/dtc/include-prefixes/*`（12 个）、
    `Documentation/Changes`。上游全树 64 个 symlink 本仓库一个都没保留，
    其中这 15 个是纯断链；`include-prefixes` 断链会让 **`make dtbs` 必然失败**
    （`vmlinux` 不经过 dtc，故此前未暴露）。
  - **13 组「仅大小写不同」的同名文件内容互相覆盖**（大小写不敏感文件系统
    clone 所致），已按上游还原。例：`include/uapi/linux/netfilter/xt_dscp.h`
    里实际写的是 `xt_DSCP.h` 的内容，头文件保护宏与结构体名全部错位；
    `Z6.0+pooncelock+poonceLock+pombonce.litmus` 的 `Result:` 断言被改错
    （`Never` → `Sometimes`）；`net/netfilter/xt_{DSCP,HL,rateest,tcpmss}.c` 同理。
  - `init/Kconfig` 中 YAT 配置块由 4 空格改为 **Tab**（符合内核 Kconfig 规范，
    即 ROADMAP A4 记录的问题）。
  - **修复后与上游的内容差异仅剩 11 个文件**（`.gitignore`、`sched.h`、`core.c`、
    `init/Kconfig`、`init_task.c`、`main.c`、`vmlinux.lds.h`、`realmode/init.c`、
    `sched/Makefile`、`uapi/sched.h`、`include/linux/sched.h`），
    全部是有意为之的 Yat 改动——「我们改了什么」有了机械可验证的边界。
    详见 [docs/CONFLICT_ANALYSIS.md](docs/CONFLICT_ANALYSIS.md) §8.6.7。
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
- `make docs-check` → **25 个 Markdown 文件 / 182 条链接 / 失效 0 条**。
- `make report`（latexmk + xelatex）→ **25 页 PDF，0 error / 0 missing character**。
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
