# Yat-CAShed QEMU 测试环境（只放脚本）

## 目录约定

本仓库对测试相关目录做了**职责切分**（A7「测试体系统一」）：

| 目录 | 只放什么 |
|------|----------|
| [**`yat_test_scripts/`**](.) | **Shell 脚本**：编译、打包 initramfs、启动 QEMU、跑 CFS vs YAT 对比 |
| [`../boot_test_scripts/`](../boot_test_scripts) | **C 源码**：`yat_simple_test.c`、`yat_bench.c`，以及说明文档 |

本目录**不放**测试程序的 `.c` 源码；`boot_test_scripts/` **不放**任何脚本与编译产物。
所有脚本都用相对路径定位内核根目录，可在任意检出位置运行。

## 快速开始

```bash
cd linux-6.8-main/yat_test_scripts

# 冒烟测试：编译 + 打包 + 启动（一步到位）
./build_yat_simple_test.sh && ./start_yat_simple_test.sh

# 性能对比：自动跑两轮 QEMU（CFS / YAT），产出 CSV
./compare_cfs_vs_yat.sh --iters 2000000

# 只需要一个能交互的 shell（不含测试程序）
./start_qemu_simple.sh
```

从仓库根目录也可以用 Makefile：

```bash
make kernel-obj        # 只编译 yat_casched.o（快速语法/类型检查）
make kernel-build      # 全量编译内核
make kernel-bench      # 编译 yat_bench（宿主机侧，产物 /tmp/yat_bench）
make kernel-compare    # 跑 CFS vs YAT 对比
```

## 脚本清单

| 脚本 | 作用 | 产物 |
|------|------|------|
| `build_yat_simple_test.sh` | 编译 `boot_test_scripts/yat_simple_test.c` 并打包 initramfs | `yat_simple_test_env/build/yat_simple_test`、`yat_simple_test_env/yat_simple_test.cpio.gz` |
| `start_yat_simple_test.sh` | 启动 QEMU 运行上面的冒烟测试 | `yat_simple_test_env/yat_simple_qemu.log` |
| `create_test_initramfs.sh` | 扫描 `boot_test_scripts/*.c` 全量编译，打包带菜单的 initramfs | `test_with_programs_build/`、`test_with_programs.cpio.gz` |
| `start_test_environment.sh` | 检查 `bzImage` 与 initramfs，缺失时自动构建，然后启动 QEMU | — |
| `start_qemu_simple.sh` | 最小环境启动（busybox shell，无测试程序） | — |
| `start_qemu_yat_test.sh` | 面向 YAT 专项测试的启动入口 | — |
| **`compare_cfs_vs_yat.sh`** | **同一内核、同一负载，分别用 CFS / YAT 跑两轮 QEMU，自动产出对比 CSV** | `results/cfs_vs_yat.csv`、`results/qemu_{cfs,yat}.log` |
| `quick_test_guide.sh` | 交互式快速测试引导 | — |
| `manual_test_guide.sh` | 手工测试步骤说明 | — |

前置条件：

1. 已编译出 `linux-6.8-main/arch/x86/boot/bzImage`（`make -j$(nproc)`）；
2. 已安装 `busybox-static`、`gcc`、`cpio`、`gzip`、`qemu-system-x86`；
3. 内核启用 `CONFIG_SCHED_CLASS_YAT_CASCHED=y`；建议同时开 `CONFIG_DEBUG_FS=y`。

```bash
sudo apt install -y busybox-static gcc cpio gzip qemu-system-x86
```

## `compare_cfs_vs_yat.sh` —— 自动对比实验

### 它解决的问题

「Yat 比 CFS 快多少」不能用两台机器、两次手工操作去比。本脚本保证：

- **同一份内核镜像**：两次运行用的都是 `arch/x86/boot/bzImage`；
- **同一份负载二进制**：`yat_bench` 只编译一次，打进同一个 initramfs；
- **同一台虚拟机的资源规格**：`-m 1024 -smp 4 -accel tcg`；
- **唯一的变量是调度策略**：通过内核命令行 `yatbench=cfs|yat` 让 guest 内的
  `yat_bench` 在启动时 `sched_setattr` 到目标策略。

并且**两次启动是两个独立的 QEMU 进程**（不是同一个 guest 内先后跑），
避免第一轮遗留的缓存热度 / 页表状态污染第二轮。

### 用法

```bash
./compare_cfs_vs_yat.sh                       # 默认 4 任务 / 2000 万次迭代 / CPU0
./compare_cfs_vs_yat.sh --iters 5000000 --tasks 8
./compare_cfs_vs_yat.sh --iters 2000000 --timeout 600 --cpus 4
./compare_cfs_vs_yat.sh --help
```

| 参数 | 默认 | 说明 |
|------|------|------|
| `--tasks N` | 4 | 并行子进程数 |
| `--iters N` | 20000000 | 每个子进程的迭代次数 |
| `--cpu N` | 0 | 所有子进程 pin 到哪个 CPU |
| `--cpus N` | 4 | QEMU `-smp` 值（guest 的 CPU 数） |
| `--kernel PATH` | `arch/x86/boot/bzImage` | 内核镜像；传 `*/vmlinux` 会自动改指 bzImage |
| `--timeout N` | 180 | 单轮 QEMU 超时（秒） |
| `--out DIR` | `results/` | 输出目录 |

### 输出

`results/cfs_vs_yat.csv`：

```csv
policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online
cfs,4,2000000,0,228582740,15985290,77244970,8000000,0,55,4
yat,4,2000000,0,61304740,12182720,19416740,8000000,0,7,4
```

以及每轮完整串口日志 `results/qemu_cfs.log`、`results/qemu_yat.log`。

### 退出码

| 码 | 含义 |
|----|------|
| 0 | 两轮都成功并产出 CSV |
| 1 | 预检失败 / QEMU 失败 / 日志里发现内核异常（`BUG:`、`KASAN:`、`possible circular locking`…） |
| 3 | 内核没有 `SCHED_YAT_CASCHED` 调度类（配置未开或镜像不对） |

### 两个实测踩到的坑（脚本已规避，但值得知道）

1. **`-kernel` 不能指向 ELF 的 `vmlinux`**
   会报 `Error loading uncompressed kernel without PVH ELF Note`。
   x86 上 QEMU 引导 ELF 需要 Xen PVH note，本内核没有。必须用 `bzImage`。

2. **不能加 `acpi=off` / `noapic` / `nolapic`**
   `-kernel bzImage` 绕过 BIOS，AP 只能靠 ACPI MADT 枚举。
   加上这些参数后 guest 会退化成 `nr_cpu_ids:1`（日志里 `Brought up 1 node, 1 CPU`），
   并行负载全挤在一个 CPU 上，对比结果无意义。
   保留 ACPI 后是 `Allowing 4 CPUs ... Brought up 1 node, 4 CPUs`，
   并且 guest 的 `poweroff -f` 能真正让 QEMU 退出（否则会一直挂到超时）。

### 结果怎么读（重要限定）

脚本会在 guest 结束后 dump A6 的 debugfs 计数器。若看到：

```
jobs_placed      0
history_records  0
accel_inserts    0
```

这**不是 bug**，而是当前实现的架构事实：`yat_ajlr_enqueue_task()` 只把 `yat_job`
绑定到 `task_struct`，并不把它插入 `global_ready_pool`；`pick_next_task_yat_casched()`
是从 `rq->yat_casched.tasks` 链表里选任务的。AJLR 就绪池目前只被
`yat_create_test_dag()` 这条演示路径使用。

因此本 CSV 测到的时间差，**来源于「调度类切换」本身**
（Yat 是一个近似 FIFO/RR 的短队列选择器，与 CFS 的红黑树 + vruntime 机制不同），
**而不是来源于 AJLR 作业分配算法**。引用数据时必须带上这个限定。

另外 TCG 纯软件模拟的单次结果噪声较大，建议 `--iters` 提到 2000 万以上，
或重复多次取中位数，不要拿单次 `min_wall_ns` 的差异下结论。

## 测试程序说明

`create_test_initramfs.sh` 会扫描 [`../boot_test_scripts/`](../boot_test_scripts) 下
**实际存在**的 `*.c` 并逐个编译到 `test_with_programs_build/`（源码目录保持干净）。
当前仓库中维护的程序：

| 程序 | 说明 |
|------|------|
| `yat_simple_test` | 5 进程并发，逐个切换到 YAT 策略并打印所在 CPU（功能验证） |
| `yat_bench` | N 进程 × M 迭代的固定工作量基准，输出 `YATCSV,...`（性能对比） |

历史文档提到的 `test_yat_core_functions`、`test_yat_hash_operations`、
`test_yat_history_table`、`test_yat_casched_complete`、`test_cache_aware_fixed`、
`verify_real_scheduling` 等程序**源码已不在仓库中**，脚本会自动跳过缺失项而不再报错。

## 验证要点

进入 guest 后：

```bash
uname -r                          # 内核版本
cat /proc/version                 # 详细构建信息
dmesg | grep -i yat_casched       # 调度类注册/初始化日志
ps aux                            # 进程状态
free                              # 内存
```

成功标准：

- 调度类初始化无错误；
- 测试进程能成功 `sched_setattr` 到 YAT 策略；
- 进程在多个 CPU 间可见迁移，且系统无 panic。

若开了 `CONFIG_DEBUG_FS`，A6 的可观测性节点：

```sh
mount -t debugfs debugfs /sys/kernel/debug
cat /sys/kernel/debug/yat_casched/stats        # 7 项 per-CPU 计数器求和
cat /sys/kernel/debug/yat_casched/ready_pool   # 就绪池快照
cat /sys/kernel/debug/yat_casched/dags         # DAG 与作业状态
```

跑 [`compare_cfs_vs_yat.sh`](compare_cfs_vs_yat.sh) 时，这些节点会被自动 dump 到日志里，
便于判断「Yat 到底有没有被真正调用」。

## 退出

在 guest shell 中输入 `exit`，再按 `Ctrl+A` 然后 `X`。

## 故障排查

| 问题 | 处理 |
|------|------|
| `找不到内核文件` | 先在 `linux-6.8-main/` 编译内核，确认 `arch/x86/boot/bzImage` 存在 |
| `Error loading uncompressed kernel without PVH ELF Note` | 用了 ELF `vmlinux`，改指 `arch/x86/boot/bzImage` |
| guest 里 `nr_cpu_ids:1` / 只有 1 个 CPU | 命令行带了 `acpi=off` / `noapic` / `nolapic`，删掉 |
| QEMU 跑到超时也不退出 | 同上（ACPI 被关掉后 `poweroff -f` 无效）；或 guest 内 panic |
| `未找到 /bin/busybox` | `sudo apt install busybox-static` |
| KVM 不可用 | 脚本已用 `-accel tcg`，纯软件模拟，较慢但可用 |
| 权限错误 | `chmod +x ./*.sh` |
| 编译失败 | 检查 `gcc` 与 `../boot_test_scripts/*.c`；失败项会被跳过并打印原因 |
| CSV 里某字段空 / `integer expression expected` | 串口是 CRLF，脚本已 `tr -d '\r'`；若自行改脚本要记得保留 |
