#!/bin/bash
#
# 一键启动「交互式」Yat-CAShed 测试环境（进 shell 手动跑命令、看 proc/debugfs）。
#
# 注意与 compare_cfs_vs_yat.sh 的分工：
#   - 本脚本 = 交互式探索，guest 里手动敲命令；
#   - compare_cfs_vs_yat.sh = 出数据（两个独立 QEMU 进程、自动落 CSV）。
#
# 两个曾经踩过的坑（本脚本已避开，勿回退）：
#   1) -kernel 必须指向 arch/x86/boot/bzImage。ELF 的 vmlinux 缺少 Xen PVH note，
#      QEMU 会报 "Error loading uncompressed kernel without PVH ELF Note"。
#   2) 不要加 acpi=off / noapic / nolapic，也不要漏掉 -smp。
#      -kernel bzImage 绕过了 BIOS，AP 只能靠 ACPI MADT 枚举；
#      关掉 ACPI 或默认 -smp 1 时 guest 只会看到 1 个 CPU，
#      此时任何「调度策略对比」都没有意义。
#
# 用法：
#   ./start_test_environment.sh          # 默认 4 核
#   SMP=2 ./start_test_environment.sh    # 指定核数
# 退出：guest 内 exit，然后 Ctrl+A 再按 X。

set -u

SMP="${SMP:-4}"

echo "=== Yat-CAShed 测试环境（交互式） ==="
echo ""

# 自动定位内核源码根目录（本脚本位于 <内核根>/yat_test_scripts）
KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$KERNEL_DIR" || exit 1

# 检查内核文件：优先 bzImage，缺失时给出明确的编译命令
KERNEL_IMAGE="arch/x86/boot/bzImage"
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "❌ 找不到 $KERNEL_IMAGE"
    echo "   请先编译内核：make -j\"\$(nproc)\" bzImage"
    echo "   （只改调度器时可先跑 make kernel/sched/yat_casched.o 做快速检查）"
    exit 1
fi

# 检查/生成 initramfs（由 create_test_initramfs.sh 产出，内含各测试程序）
OUT_IMAGE="test_with_programs.cpio.gz"
if [ ! -f "$OUT_IMAGE" ]; then
    echo "🔧 未找到 $OUT_IMAGE，正在生成..."
    ( cd yat_test_scripts && ./create_test_initramfs.sh ) || {
        echo "❌ initramfs 生成失败"
        exit 1
    }
fi

if [ ! -f "$OUT_IMAGE" ]; then
    echo "❌ 仍未找到 $OUT_IMAGE（路径：$KERNEL_DIR/$OUT_IMAGE）"
    exit 1
fi

echo "🚀 启动 QEMU"
echo "   - 内核:     $KERNEL_IMAGE"
echo "   - initramfs:$OUT_IMAGE"
echo "   - CPU:      smp=$SMP（不要设为 1，否则测不出任何跨核行为）"
echo "   - 内存:     1024MB"
echo ""
echo "💡 guest 内可直接运行："
echo "   - /bin/yat_simple_test     功能冒烟（历史表/加速表/作业生命周期）"
echo "   - /bin/yat_bench --help    负载基准（参数：--policy/--tasks/--iters/--affinity/--sleep-us）"
echo "   - cat /sys/kernel/debug/yat_casched/stats   调度类计数器"
echo "   - 退出：exit → Ctrl+A → X"
echo ""

qemu-system-x86_64 \
    -nographic \
    -m 1024 \
    -smp "$SMP" \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$OUT_IMAGE" \
    -no-reboot \
    -accel tcg \
    -append "console=ttyS0,115200 panic=1 nokaslr"
