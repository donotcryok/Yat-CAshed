# YAT_CASCHED 极简测试环境

本目录提供一套最小可用的 QEMU 启动测试环境，用于验证
`CONFIG_SCHED_CLASS_YAT_CASCHED` 是否生效以及任务能否被调度到不同 CPU。

> 完整说明见 [`README.md`](README.md)，本文仅保留最简操作步骤。

## 文件说明

| 文件 | 作用 |
|------|------|
| `yat_simple_test.c` | 测试程序：创建 5 个进程，逐个调用 `sched_setattr` 切到 YAT 策略，打印 `sched_getcpu()` 结果 |
| `build_yat_simple_test.sh` | 编译测试程序并打包 `yat_simple_test.cpio.gz` |
| `start_yat_simple_test.sh` | 启动 QEMU（512MB / 4 vCPU / serial 控制台） |

## 使用步骤

```bash
# 在内核根目录之外也可以执行，脚本内部使用相对路径
cd linux-6.8-main/boot_test_scripts

# 1) 构建（编译 + 打包 initramfs）
./build_yat_simple_test.sh

# 2) 启动
./start_yat_simple_test.sh
```

若脚本不可执行，先执行 `chmod +x ./*.sh`。

## 预期输出

1. 内核启动日志；
2. `yat_casched` 调度类注册/初始化信息；
3. 5 个测试进程各自的 PID、当前 CPU、策略设置结果；
4. 进程在 CPU 之间的迁移记录。

## 故障排查

| 现象 | 处理 |
|------|------|
| `内核文件不存在` | 先在 `linux-6.8-main/` 编译内核，确认生成 `vmlinux` |
| `initramfs文件不存在` | 先执行 `./build_yat_simple_test.sh` |
| 策略设置失败（`EINVAL`） | 确认内核配置启用 `CONFIG_SCHED_CLASS_YAT_CASCHED`，且调度类编号与用户态一致 |
| 无输出 | 查看同目录下 `yat_simple_qemu.log` |

## 退出 QEMU

`Ctrl+A` 然后按 `X`；或在 guest shell 中执行 `halt`。
