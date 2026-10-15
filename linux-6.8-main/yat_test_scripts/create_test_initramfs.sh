#!/bin/bash

# 创建包含测试程序的 initramfs
echo "=== 创建包含 Yat_Casched 测试程序的 initramfs ==="

# 自动定位内核源码根目录（本脚本位于 <内核根>/yat_test_scripts）
KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$KERNEL_DIR"

# 检查测试程序是否存在，如果不存在则编译
echo "🔧 检查和编译测试程序..."

# 编译测试程序
if [ ! -f "boot_test_scripts/test_yat_casched_complete" ]; then
    echo "编译 test_yat_casched_complete..."
    gcc -O2 -static -o boot_test_scripts/test_yat_casched_complete boot_test_scripts/test_yat_casched_complete.c
fi

if [ ! -f "boot_test_scripts/test_cache_aware_fixed" ]; then
    echo "编译 test_cache_aware_fixed..."
    gcc -O2 -static -o boot_test_scripts/test_cache_aware_fixed boot_test_scripts/test_cache_aware_fixed.c
fi

if [ ! -f "boot_test_scripts/verify_real_scheduling" ]; then
    echo "编译 verify_real_scheduling..."
    gcc -O2 -static -o boot_test_scripts/verify_real_scheduling boot_test_scripts/verify_real_scheduling.c
fi

# 编译新的核心功能测试
if [ ! -f "boot_test_scripts/test_yat_core_functions" ]; then
    echo "编译 test_yat_core_functions..."
    gcc -O2 -static -o boot_test_scripts/test_yat_core_functions boot_test_scripts/test_yat_core_functions.c
fi

if [ ! -f "boot_test_scripts/test_yat_hash_operations" ]; then
    echo "编译 test_yat_hash_operations..."
    gcc -O2 -static -o boot_test_scripts/test_yat_hash_operations boot_test_scripts/test_yat_hash_operations.c
fi

if [ ! -f "boot_test_scripts/test_yat_history_table" ]; then
    echo "编译 test_yat_history_table..."
    gcc -O2 -static -o boot_test_scripts/test_yat_history_table boot_test_scripts/test_yat_history_table.c
fi

# 创建包含测试程序的 initramfs
echo "📦 创建包含测试程序的 initramfs..."

rm -rf test_with_programs_initramfs
mkdir -p test_with_programs_initramfs/{bin,sbin,etc,proc,sys,dev,tmp,usr/bin}

# 复制静态 busybox
cp /bin/busybox test_with_programs_initramfs/bin/
chmod +x test_with_programs_initramfs/bin/busybox

# 复制测试程序
cp boot_test_scripts/test_yat_casched_complete test_with_programs_initramfs/bin/
cp boot_test_scripts/test_cache_aware_fixed test_with_programs_initramfs/bin/
cp boot_test_scripts/verify_real_scheduling test_with_programs_initramfs/bin/
cp boot_test_scripts/test_yat_core_functions test_with_programs_initramfs/bin/
cp boot_test_scripts/test_yat_hash_operations test_with_programs_initramfs/bin/
cp boot_test_scripts/test_yat_history_table test_with_programs_initramfs/bin/
chmod +x test_with_programs_initramfs/bin/test_*
chmod +x test_with_programs_initramfs/bin/verify_*

# 创建增强的 init 脚本
cat > test_with_programs_initramfs/init << 'EOF'
#!/bin/busybox sh

echo "=== Yat_Casched 测试环境启动成功 ==="
echo "内核版本: $(busybox uname -r)"

# 挂载基本文件系统
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys  
/bin/busybox mount -t devtmpfs devtmpfs /dev

# 安装 busybox 命令
/bin/busybox --install -s

echo ""
echo "🎯 Yat_Casched 调度器测试环境"
echo "================================"
echo "内核: $(uname -r)"
echo "调度器: Yat_Casched (Cache-Aware Scheduler)"
echo ""

echo "📋 可用的测试程序："
echo "=== 核心功能测试 ==="
echo "1. test_yat_core_functions     - 调度器核心功能测试"
echo "2. test_yat_hash_operations    - 哈希表/历史表 CRUD 测试"
echo "3. test_yat_history_table      - 专门历史表 CRUD 测试"
echo ""
echo "=== 性能和兼容性测试 ==="
echo "4. test_yat_casched_complete   - 完整调度器测试"
echo "5. test_cache_aware_fixed      - 缓存感知测试"  
echo "6. verify_real_scheduling      - 实时调度验证"
echo ""

echo "📋 基本命令："
echo "- ps        : 查看进程"
echo "- top       : 系统监控"
echo "- uptime    : 系统运行时间"
echo "- free      : 内存使用情况"
echo "- cat /proc/version : 详细内核信息"
echo "- exit      : 退出"
echo ""

echo "🚀 快速测试："
echo "# 核心功能测试"
echo "test_yat_core_functions"
echo "test_yat_hash_operations"
echo "test_yat_history_table"
echo ""
echo "# 完整测试套件"
echo "test_yat_casched_complete"
echo "test_cache_aware_fixed"  
echo "verify_real_scheduling"
echo ""

echo "=== 进入交互式 shell ==="
exec /bin/busybox sh
EOF

chmod +x test_with_programs_initramfs/init

# 创建 initramfs
echo "🗜️ 打包 initramfs..."
cd test_with_programs_initramfs
find . | cpio -o -H newc | gzip > ../test_with_programs.cpio.gz
cd ..

echo "✅ 包含测试程序的 initramfs 创建完成: test_with_programs.cpio.gz"
echo ""
echo "🚀 使用方法："
echo "qemu-system-x86_64 -nographic -m 1024 -kernel vmlinux -initrd test_with_programs.cpio.gz -append \"console=ttyS0,115200\" -accel tcg"
