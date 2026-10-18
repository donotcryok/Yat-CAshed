# YAT_CASCHED 极简冒烟测试环境

本文件是 [`README.md`](README.md) 的**最简操作版**，只讲「怎么把冒烟测试跑起来」。
完整目录约定、`yat_bench.c` 的 CSV 字段说明见 [`README.md`](README.md)。

## 目录约定（先看这条）

`boot_test_scripts/` **只放 C 源码**。所有 Shell 脚本（编译 / 打包 / 启动 QEMU / 跑对比）
都在 [`../yat_test_scripts/`](../yat_test_scripts)。编译产物落在
`test_with_programs_build/` 或 `yat_simple_test_env/build/`，不会污染本目录。

| 文件 | 作用 |
|------|------|
| `yat_simple_test.c` | 冒烟测试程序：创建 5 个进程，逐个 `sched_setattr` 切到 YAT 策略，打印所在 CPU |
| `yat_bench.c` | 性能基准程序：N 进程 × M 迭代，输出 `YATCSV,...` 机器可解析行 |
| `README.md` | 完整说明（目录约定、CSV 字段、用法） |
| `SIMPLE_TEST_README.md` | 本文件 |

## 使用步骤

脚本会自己用相对路径定位内核根目录，所以在哪个 `cwd` 执行都可以：

```bash
cd linux-6.8-main/yat_test_scripts

# 1) 构建（编译 + 打包 initramfs）
./build_yat_simple_test.sh

# 2) 启动
./start_yat_simple_test.sh
```

若提示权限不足，先 `chmod +x ../yat_test_scripts/*.sh`。

## 预期输出

1. 内核启动日志（`-kernel arch/x86/boot/bzImage`，4 vCPU，ttyS0 串口）；
2. `yat_casched` 调度类的注册 / 初始化信息；
3. 5 个测试进程各自的 PID、当前 CPU、策略设置结果；
4. 进程在 CPU 之间的迁移记录。

若同时开了 `CONFIG_DEBUG_FS`，可在 guest 内查看 A6 的可观测性节点：

```sh
mount -t debugfs debugfs /sys/kernel/debug
cat /sys/kernel/debug/yat_casched/stats
cat /sys/kernel/debug/yat_casched/ready_pool
cat /sys/kernel/debug/yat_casched/dags
```

## 故障排查

| 现象 | 处理 |
|------|------|
| `内核文件不存在` | 先在 `linux-6.8-main/` 编译内核，确认生成 `arch/x86/boot/bzImage` |
| `initramfs文件不存在` | 先执行 `../yat_test_scripts/build_yat_simple_test.sh` |
| `Error loading uncompressed kernel without PVH ELF Note` | 误用了 ELF 的 `vmlinux`；`-kernel` 必须指向 `arch/x86/boot/bzImage` |
| guest 里只有 1 个 CPU | 内核命令行被加了 `acpi=off` / `noapic` / `nolapic`；`-kernel` 绕过 BIOS，AP 只能靠 ACPI MADT 枚举，必须保留 ACPI |
| 策略设置失败（`EINVAL`） | 确认内核配置启用 `CONFIG_SCHED_CLASS_YAT_CASCHED`，且用户态用的编号与内核一致（当前为 8） |
| 无输出 | 查看 `yat_simple_test_env/yat_simple_qemu.log` |

## 退出 QEMU

`Ctrl+A` 然后按 `X`；或在 guest shell 中执行 `halt`。
