#!/bin/bash
#
# YAT_CASCHED QEMU 启动脚本（极简测试环境）
#
# 目录职责约定（A7 统一后）：
#   boot_test_scripts/   只放 C 源码
#   yat_test_scripts/    只放 QEMU / initramfs 编排脚本
#
# 本脚本原位于 boot_test_scripts/，A7 迁移到此处。initramfs 与日志都位于
# <内核根>/yat_simple_test_env/（已被 .gitignore 忽略）。

set -e

echo "=== YAT_CASCHED QEMU 测试启动 ==="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL="$WORK_DIR/vmlinux"
TEST_DIR="$WORK_DIR/yat_simple_test_env"
INITRAMFS="$TEST_DIR/yat_simple_test.cpio.gz"
LOG_FILE="$TEST_DIR/yat_simple_qemu.log"

if [ ! -f "$KERNEL" ]; then
    echo "错误: 内核文件不存在: $KERNEL"
    echo "请先编译内核: cd $WORK_DIR && make -j\$(nproc)"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "错误: initramfs 文件不存在: $INITRAMFS"
    echo "请先运行: $SCRIPT_DIR/build_yat_simple_test.sh"
    exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "错误: 未找到 qemu-system-x86_64"
    echo "请安装: sudo apt install qemu-system-x86"
    exit 1
fi

echo "内核文件: $KERNEL"
echo "Initramfs: $INITRAMFS"
echo "日志文件: $LOG_FILE"
echo ""
echo "内存: 512MB    CPU 核心: 4    控制台: ttyS0"
echo "启动 QEMU（退出：Ctrl+A 然后 X）..."
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 512M \
    -smp 4 \
    -nographic \
    -no-reboot \
    -append "console=ttyS0 panic=1 nokaslr noapic acpi=off" \
    2>&1 | tee "$LOG_FILE"

echo ""
echo "QEMU 已退出"
echo "查看完整日志: cat $LOG_FILE"
