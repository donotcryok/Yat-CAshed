# 操作手册（OPERATIONS）

本文件是**可照着敲的**操作清单：从「改完代码」到「拿到可信数据」再到「提交」。
架构与设计理由见 [`ARCHITECTURE.md`](ARCHITECTURE.md)，任务清单见 [`ROADMAP.md`](ROADMAP.md)，
所有命令都在仓库根目录 `Yat-CAShed/` 下执行（除特别标注 `cd` 的步骤）。

> 约定：命令块中的 `$` 表示普通 shell 提示符，不要复制。所有内核命令都假定已配置
> `CONFIG_SCHED_CLASS_YAT_CASCHED=y`（仓库内的 `linux-6.8-main/.config` 已就位）。

---

## 0. 三分钟自检（改完代码先跑这个）

```bash
make kernel-obj      # 只编译 kernel/sched/yat_casched.o —— 最快的语法/类型检查
make check           # 冲突标记 / 空文件 / 关键文件守卫
make docs-check      # 全仓库 Markdown 相对链接（失效即失败）
make symbol-check    # yat_casched.h 声明 vs yat_casched.c 定义
```

`make kernel-obj` 通过之后再做全量编译，能省掉一次十几分钟的链接才发现拼写错误的往返。

---

## 1. 编译内核

```bash
cd linux-6.8-main
make -j"$(nproc)" bzImage          # 产物：arch/x86/boot/bzImage
```

- 增量编译：只改 `kernel/sched/*` 时通常 2–3 分钟。
- 改了 `include/linux/sched/yat_casched.h` 会触发大面积重编（`sched.h` 依赖），
  预留 15–20 分钟；这是正常的，不是编译系统坏了。
- 只想要「能跑」的最小产物时，`bzImage` 足够；测试脚本只认它，不认 `vmlinux`。

## 2. 构建并冒烟测试用户态基准

```bash
make kernel-bench                  # gcc 编译 boot_test_scripts/yat_bench.c -> /tmp/yat_bench
/tmp/yat_bench --help              # 确认参数被正确解析
cd linux-6.8-main/yat_test_scripts
bash -n compare_cfs_vs_yat.sh      # 改过脚本必做：语法检查
```

---

## 3. 验证 A8（AJLR 是否真的接管选核）

### 3.1 快速冒烟（约 2 分钟）

```bash
cd linux-6.8-main/yat_test_scripts
./compare_cfs_vs_yat.sh --tasks 4 --iters 200000 \
                        --affinity none --sleep-us 200 --timeout 120
```

### 3.2 正式测量（约 5 分钟）

```bash
./compare_cfs_vs_yat.sh --tasks 8 --iters 2000000 \
                        --affinity none --sleep-us 200 --timeout 300
```

**两个参数缺一不可**，否则测不到 AJLR：

| 参数 | 为什么必须 |
|------|-----------|
| `--affinity none` | `shared` 会把 `p->nr_cpus_allowed` 压到 1，`core.c` 的 `select_task_rq` 包装层直接 `cpumask_any()` 返回，**不调用调度类回调** |
| `--sleep-us 200` | 纯 busy loop 只有 `WF_FORK`，而 `select_task_rq_yat_casched()` 刻意在非 `WF_TTWU` 早退 |

脚本会在结束时自动打印这段判定，看到 ✅ 才说明这组数据可以评价 AJLR。

### 3.3 期望输出（实测基线，kernel #14，smp=4）

```
ttwu_ok            512        # = 8 任务 × 64 次 sleep，与负载完全吻合
select_calls       459
select_decided     254
jobs_placed        254
place_hit          925
place_miss         0
place_pending      14
pick_null_desync   0
```

接管率由脚本算出：`925 / (925+0+14) ≈ 98%`。

---

## 4. 读结果：每个计数器回答什么问题

完整字段说明见 [`ARCHITECTURE.md`](ARCHITECTURE.md) §3.1.1。排障时按链路顺序看：

```
enq_calls ──► ttwu_* ──► select_* ──► jobs_placed ──► place_*
（进了类吗） （唤醒走到哪） （回调走到哪） （算出来了吗） （执行了吗）
```

| 观察 | 含义 | 下一步 |
|------|------|--------|
| `enq_calls = 0` | 任务根本没进 yat 类 | 查 `sched_setscheduler` 是否成功、`__setscheduler_prio()` 是否被 `rt_prio()` 抢先 |
| `enq_calls > 0` 但 `ttwu_ok = 0` | 没有一次正常唤醒 | 查 `--sleep-us` 是否为 0、负载是否真的阻塞 |
| `ttwu_ok > 0` 但 `select_calls = 0` | 唤醒没走到选核 | 看 `ttwu_state_bail/runnable/wakelist` 哪个非零 |
| `select_calls > 0` 但 `select_decided = 0` | 每次都在节流/无核处退出 | 看 `select_sticky_*` 与 `select_nocore` |
| **`pick_null_desync > 0`** | 核心层说有任务、本类说没有 | 几乎一定是漏调 `add_nr_running()/sub_nr_running()`，CPU 会直接进 idle（静默挂死） |
| `place_hit` 占比低 | AJLR 的决策被亲和性/兜底路径否决 | 查 tracepoint `_wakeup_place` 的 `(stay)/(migrate)` |

`placements` 节点是**实时快照**：任务退出时 job 被释放，workload 结束后必然为空。
**可留存的接管率证据是 `place_hit/miss/pending` 三个累计计数器。**

---

## 5. 故障排查速查

| 现象 | 常见原因 | 处理 |
|------|----------|------|
| guest 无任何输出、QEMU 到点被杀 | CPU 全部 idle 但任务仍 `on_rq`（计数失步） | 读 `pick_null_desync`；检查 enqueue/dequeue 是否成对维护 `rq->nr_running` |
| `nr_cpu_ids 1` | 内核命令行误加 `acpi=off`/`noapic`/`nolapic` | 去掉它们；AP 只能靠 ACPI MADT 枚举 |
| 脚本报 `integer expression expected` | 串口日志是 CRLF，解析时残留 `\r` | 解析处必须 `tr -d '\r'`（脚本内已统一处理） |
| `jobs_placed = 0` 且 `select_calls = 0` | 用了默认 `--affinity shared` | 改 `--affinity none --sleep-us 200` |
| `scripts/config: not found` | 在仓库根目录执行了内核命令 | 先 `cd linux-6.8-main` |
| 构建中途进程被 SIGKILL | 并行度过高导致内存不足 | `make -j4`（而不是 `-j$(nproc)`），或后台跑并 `tail -f` 日志 |

> TCG 纯软件模拟下单次测量噪声很大（同一版本重复 3 次，`sum_wall` 可有 ±10% 波动）。
> 任何结论都应以**多次重复的中位数**为准，不要用单次差异下判断。

---

## 6. 补丁工作流（内核整树不入库）

```bash
# 校验现有补丁包（SHA256 + hunk 数）
make patch-check

# 往返自检：上游 -> 打补丁 -> 与仓库逐字节比对 -> 反向回退 -> 与上游比对
# 需要一份未修改的 v6.8 源码树（只用到其中 11 个文件）
scripts/gen_kernel_patches.sh --upstream /tmp/up --out patches   # 重新生成
make patch-selftest UPSTREAM=/tmp/up

# 在干净的 v6.8 树上应用/回退/查看状态
make patch-apply
make patch-status
make patch-reverse
```

**改了内核树里任何受补丁管理的文件后，必须重新生成补丁并跑一次 `patch-selftest`**，
否则补丁与源码会悄悄分叉。受管文件清单见
[`../patches/README.md`](../patches/README.md) 与
[`../scripts/gen_kernel_patches.sh`](../scripts/gen_kernel_patches.sh) 里的
`UPSTREAM_FILES` / `NEW_FILES`。

---

## 7. 提交前清单

```bash
make verify-all      # check + docs-check + name-check + symbol-check + patch-check + 模拟器全套
git status --porcelain
```

- [ ] `make verify-all` 全绿（模拟器部分约 1–2 分钟）
- [ ] 改了内核受管文件 → 补丁已重新生成且 `patch-selftest` 通过
- [ ] 改了测试脚本 → `bash -n` 通过，并至少跑过一次「快速冒烟」
- [ ] 文档中出现的数字均来自**本次实测**，不是上一次的残留
- [ ] 提交信息遵循 [`../CONTRIBUTING.md`](../CONTRIBUTING.md) §2（`feat/fix/test/docs/chore` + 中文描述）

---

## 8. 一页速查

```bash
make kernel-obj                                   # 最快语法检查
make kernel-build                                 # 全量编译
make kernel-bench                                 # 只编用户态基准
make kernel-compare                               # 默认参数对比（仅作冒烟）
./linux-6.8-main/yat_test_scripts/compare_cfs_vs_yat.sh \
    --tasks 8 --iters 2000000 --affinity none --sleep-us 200   # A8 正式测量
make patch-check patch-selftest UPSTREAM=/tmp/up  # 补丁完整性
make verify-all                                   # 提交前门禁
```
