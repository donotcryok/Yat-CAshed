#!/bin/bash

# Yat-CAShed 快速测试脚本
# 使用静态 busybox 启动 QEMU 测试环境

# 自动定位内核源码根目录（本脚本位于 <内核根>/yat_test_scripts）
KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$KERNEL_DIR"

echo "=== Yat-CAShed 快速测试启动 ==="
echo "时间: $(date)"

# 检查内核文件
if [ ! -f "vmlinux" ]; then
    echo "❌ 错误: 找不到内核文件 vmlinux"
    echo "请先编译内核：make -j$(nproc)"
    exit 1
fi

# 使用现有的静态 initramfs 或创建新的
if [ ! -f "test_static.cpio.gz" ]; then
    echo "🔧 创建静态 initramfs..."
    
    # 创建目录结构
    rm -rf test_static_initramfs
    mkdir -p test_static_initramfs/{bin,sbin,etc,proc,sys,dev,tmp,usr/bin}
    
    # 复制静态 busybox
    if [ ! -f "/bin/busybox" ]; then
        echo "安装 busybox-static..."
        sudo apt update && sudo apt install -y busybox-static
    fi
    
    cp /bin/busybox test_static_initramfs/bin/
    chmod +x test_static_initramfs/bin/busybox
    
    # 创建 init 脚本
    cat > test_static_initramfs/init << 'EOF'
#!/bin/busybox sh

echo "=== Yat-CAShed 测试环境启动成功 ==="
echo "内核版本: $(busybox uname -r)"
echo "当前时间: $(busybox date)"

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys  
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 安装 busybox 命令
/bin/busybox --install -s

echo ""
echo "🎯 Yat-CAShed 调度器信息:"
echo "- 内核版本: $(uname -r)"
echo "- 调度器类型: Yat-CAShed (Cache-Aware Scheduler)"

echo ""
echo "📋 可用命令："
echo "- ps        : 查看进程"
echo "- top       : 系统监控"
echo "- uptime    : 系统运行时间"
echo "- free      : 内存使用情况"
echo "- cat /proc/version : 详细内核信息"
echo "- exit      : 退出"

echo ""
echo "=== 进入交互式 shell ==="
exec /bin/busybox sh
EOF
    
    chmod +x test_static_initramfs/init
    
    # 创建 initramfs
    cd test_static_initramfs
    find . | cpio -o -H newc | gzip > ../test_static.cpio.gz
    cd ..
    
    echo "✅ initramfs 创建完成"
fi

# 内核镜像必须是 bzImage：ELF 的 vmlinux 缺少 Xen PVH note，QEMU 无法直接启动。
KERNEL_IMAGE="arch/x86/boot/bzImage"
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "❌ 找不到 $KERNEL_IMAGE，请先执行：make -j\"\$(nproc)\" bzImage"
    exit 1
fi

echo ""
echo "🚀 启动 QEMU 虚拟机..."
echo "   - 内核: $KERNEL_IMAGE"
echo "   - 内存: 1024MB    CPU 核心: 4"
echo "   - 加速: TCG"
echo "   - 串口: ttyS0"
echo ""
echo "💡 提示: 要退出 QEMU，请在虚拟机中输入 'exit'，然后按 Ctrl+A 再按 X"
echo ""

# 启动 QEMU
# 注意：不要加 acpi=off / noapic / nolapic，也不要漏掉 -smp。
# -kernel bzImage 绕过 BIOS，AP 只能靠 ACPI MADT 枚举；关掉 ACPI 后 guest
# 只会看到 1 个 CPU，任何跨核调度观察都会失真。
qemu-system-x86_64 \
    -nographic \
    -m 1024 \
    -smp 4 \
    -kernel "$KERNEL_IMAGE" \
    -initrd test_static.cpio.gz \
    -no-reboot \
    -append "console=ttyS0,115200 panic=1 nokaslr quiet" \
    -accel tcg
