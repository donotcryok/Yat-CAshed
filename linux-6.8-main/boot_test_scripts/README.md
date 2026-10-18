# Yat_Casched 启动测试脚本

本目录只包含**一套**可用的极简 QEMU 启动测试环境。早期文档中提到的
`create_initramfs_complete.sh`、`start_with_template.sh`、`test_yat_casched_complete.c` 等文件
已不存在，请以本文件与实际目录为准。

## 目录内容

```
boot_test_scripts/
├── yat_simple_test.c          # 用户态测试程序（5 个进程并发 + sched_setattr 切换策略）
├── build_yat_simple_test.sh   # 编译测试程序并打包 initramfs
├── start_yat_simple_test.sh   # 启动 QEMU（-smp 4, ttyS0 控制台）
├── SIMPLE_TEST_README.md      # 同一环境的说明（历史命名，内容与本节一致）
└── README.md                  # 本文件
```

> 运行 `build_yat_simple_test.sh` 后会额外生成 `yat_simple_test.cpio.gz` 与
> `yat_simple_qemu.log`，二者均为构建产物，不纳入版本控制。

## 使用方法

```bash
cd linux-6.8-main/boot_test_scripts

# 1) 编译测试程序 + 打包 initramfs
./build_yat_simple_test.sh

# 2) 启动 QEMU
./start_yat_simple_test.sh
```

前置条件：

1. 内核已编译，存在 `linux-6.8-main/vmlinux`（脚本会检查并给出提示）；
2. 已安装 `qemu-system-x86_64`、`gcc`、`cpio`；
3. 内核配置已启用 `CONFIG_SCHED_CLASS_YAT_CASCHED`。

## 脚本做了什么

[`start_yat_simple_test.sh`](start_yat_simple_test.sh) 使用相对路径定位内核根目录，
不依赖任何绝对路径：

| 变量 | 解析方式 |
|------|----------|
| `SCRIPT_DIR` | 脚本所在目录 |
| `WORK_DIR` | 内核根目录（`SCRIPT_DIR/..`） |
| `KERNEL` | `$WORK_DIR/vmlinux` |
| `INITRAMFS` | `$SCRIPT_DIR/yat_simple_test.cpio.gz` |

QEMU 参数：`-m 512M -smp 4 -nographic -no-reboot`，
内核命令行 `console=ttyS0 panic=1 nokaslr noapic acpi=off`。

## 退出

在 QEMU 控制台中按 `Ctrl+A` 再按 `X`，或在 guest shell 中执行 `halt`。

## 相关文档

- 更多测试脚本：[`../yat_test_scripts/README.md`](../yat_test_scripts/README.md)
- 性能测试与图表：[`../test_visualization/README.md`](../test_visualization/README.md)
- 内核构建说明：[`../../docs/DEVELOPMENT.md`](../../docs/DEVELOPMENT.md)
