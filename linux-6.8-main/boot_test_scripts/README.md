# Yat-CAShed 用户态测试程序（只放 C 源码）

## 目录约定（重要）

本仓库对测试相关目录做了**职责切分**，这是 A7「测试体系统一」的成果：

| 目录 | 只放什么 | 不放什么 |
|------|----------|----------|
| [`boot_test_scripts/`](.) | **C 源码**（`*.c`）与本说明 | 任何编译产物、任何 `.sh` 脚本 |
| [`../yat_test_scripts/`](../yat_test_scripts) | **Shell 脚本**（编译 / 打包 initramfs / 启动 QEMU / 跑对比） | 测试程序的 C 源码 |

早期版本把 `.sh` 脚本放在本目录，并且在 `gcc -o "$BOOT_DIR/$name"` 时把二进制直接写到
源码目录里。这两点都已修正：

- 四个脚本已 `git mv` 迁到 [`../yat_test_scripts/`](../yat_test_scripts)；
- [`create_test_initramfs.sh`](../yat_test_scripts/create_test_initramfs.sh) 现在把可执行文件编译到
  `test_with_programs_build/`（已 gitignore），本目录**物理上**只剩 `.c` 与 Markdown。

因此，如果在本目录看到可执行文件，说明是历史残留，可安全删除。

## 目录内容

```
boot_test_scripts/
├── yat_simple_test.c        # 冒烟测试：5 进程并发，逐个 sched_setattr 切到 YAT 策略
├── yat_bench.c              # 性能基准：N 进程 × M 次迭代，输出机器可解析的 YATCSV 行
├── README.md                # 本文件
└── SIMPLE_TEST_README.md    # 冒烟测试的最简操作步骤（历史命名，内容仍有效）
```

## 两个程序分别做什么

### `yat_simple_test.c` —— 能不能跑起来

创建若干子进程，逐个用 `sched_setattr(2)` 把调度策略切到 `SCHED_YAT_CASCHED`（编号 8），
打印每个进程的 PID、`sched_getcpu()` 结果与策略设置返回值。用途是**功能性验证**：
调度类是否注册、`sched_setattr` 是否被接受、任务是否真的被调度到 CPU 上。

### `yat_bench.c` —— 快不快

同一份固定工作量（LCG + 异或混合，避免被编译器优化掉），在指定 CPU 上跑 `iters` 次，
N 个子进程并行，父进程通过 pipe 收回每个子进程的墙钟时间，聚合成一行 CSV：

```
YATCSV,policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online
```

字段含义：

| 字段 | 说明 |
|------|------|
| `policy` | `cfs` 或 `yat`（由内核命令行 `yatbench=` 传入） |
| `sum_wall_ns` | N 个子进程墙钟时间之和（ns） |
| `min/max_wall_ns` | 单个子进程的最快 / 最慢墙钟时间（反映离散度） |
| `sum_work_units` | 校验和：应为 `tasks × iters`，用于确认真的跑完了 |
| `vol_ctx` / `invol_ctx` | 自愿 / 非自愿上下文切换次数（读 `/proc/self/status`） |
| `cpus_online` | guest 内 `_SC_NPROCESSORS_ONLN`；**必须 > 1**，否则对比无意义 |

退出码：`0` 成功；`2` 参数或调用失败；`3` 当前内核没有 `SCHED_YAT_CASCHED`。

> 该程序被 [`compare_cfs_vs_yat.sh`](../yat_test_scripts/compare_cfs_vs_yat.sh) 在 guest 内调用，
> 由内核命令行参数决定跑 CFS 还是 YAT，从而保证**同一二进制、同一内核、同一负载**，
> 唯一变量是调度策略。

## 怎么用

所有操作都在 [`../yat_test_scripts/`](../yat_test_scripts) 里发起：

```bash
cd linux-6.8-main/yat_test_scripts

# 冒烟测试：编译 + 打包 initramfs + 启动 QEMU
./build_yat_simple_test.sh
./start_yat_simple_test.sh

# 性能对比：自动跑 CFS 与 YAT 两轮 QEMU，产出 CSV
./compare_cfs_vs_yat.sh --iters 2000000

# 或者从仓库根目录用 Makefile
make kernel-compare
```

手工编译单个程序（产物**不要**落在本目录）：

```bash
gcc -O2 -static -Wall -Wextra \
    -o /tmp/yat_bench linux-6.8-main/boot_test_scripts/yat_bench.c
```

## 前置条件

1. 内核已编译出 `arch/x86/boot/bzImage`（见 [`../../docs/DEVELOPMENT.md`](../../docs/DEVELOPMENT.md)）；
2. 已安装 `qemu-system-x86`、`gcc`、`cpio`、`gzip`、`busybox-static`；
3. 内核配置启用 `CONFIG_SCHED_CLASS_YAT_CASCHED=y`，
   并建议同时开 `CONFIG_DEBUG_FS=y`（A6 的可观测性节点依赖它）。

## 相关文档

- 脚本清单与用法：[`../yat_test_scripts/README.md`](../yat_test_scripts/README.md)
- 性能对比实验设计与结论：[`../../docs/EXPERIMENT_REPORT.md`](../../docs/EXPERIMENT_REPORT.md)
- 性能测试与图表：[`../test_visualization/README.md`](../test_visualization/README.md)
- 内核构建说明：[`../../docs/DEVELOPMENT.md`](../../docs/DEVELOPMENT.md)
