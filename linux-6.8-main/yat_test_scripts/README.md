# Yat_Casched QEMU 测试环境

本目录提供在 QEMU 中启动自带 Yat_Casched 调度类内核的脚本。
所有脚本均使用**相对路径**定位内核根目录，可在任意检出位置运行。

## 快速开始

```bash
cd linux-6.8-main/yat_test_scripts

# 推荐：一步启动（缺 initramfs 会自动构建）
./start_test_environment.sh
```

手工分步：

```bash
# 1) 构建带测试程序的 initramfs（会编译 boot_test_scripts/*.c）
./create_test_initramfs.sh

# 2) 启动
qemu-system-x86_64 -nographic -m 1024 -kernel ../vmlinux \
  -initrd ../test_with_programs.cpio.gz \
  -append "console=ttyS0,115200 noapic nolapic acpi=off" -accel tcg
```

仅需基础 shell（不含测试程序）：

```bash
./start_qemu_simple.sh
```

## 脚本清单

| 脚本 | 作用 |
|------|------|
| `create_test_initramfs.sh` | 编译 `boot_test_scripts/*.c`，打包 `test_with_programs.cpio.gz` |
| `start_test_environment.sh` | 检查 `vmlinux` 与 initramfs，缺失时自动构建，然后启动 QEMU |
| `start_qemu_simple.sh` | 最小环境启动（无测试程序） |
| `start_qemu_yat_test.sh` | 面向 YAT 专项测试的启动入口 |
| `quick_test_guide.sh` | 交互式快速测试引导 |
| `manual_test_guide.sh` | 手工测试步骤说明 |

前置条件：

1. 已在 `linux-6.8-main/` 编译出 `vmlinux`（`make -j$(nproc)`）；
2. 已安装 `busybox-static`、`gcc`、`cpio`、`gzip`、`qemu-system-x86`；
3. 内核启用 `CONFIG_SCHED_CLASS_YAT_CASCHED`。

```bash
sudo apt update
sudo apt install -y busybox-static gcc cpio gzip qemu-system-x86
```

## 测试程序说明

`create_test_initramfs.sh` 会扫描 [`boot_test_scripts/`](../boot_test_scripts) 下**实际存在**的
`*.c` 并逐个编译；当前仓库中维护的测试程序为：

| 程序 | 说明 |
|------|------|
| `yat_simple_test` | 5 进程并发，逐个切换到 YAT 策略并打印所在 CPU |

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

## 退出

在 guest shell 中输入 `exit`，再按 `Ctrl+A` 然后 `X`。

## 故障排查

| 问题 | 处理 |
|------|------|
| `找不到内核文件 vmlinux` | 先在 `linux-6.8-main/` 编译内核 |
| `未找到 /bin/busybox` | `sudo apt install busybox-static` |
| KVM 不可用 | 脚本已使用 `-accel tcg`，纯软件模拟，较慢但可用 |
| 权限错误 | `chmod +x ./*.sh` |
| 编译失败 | 检查 `gcc` 与 `boot_test_scripts/*.c`；失败项会被跳过并打印原因 |
