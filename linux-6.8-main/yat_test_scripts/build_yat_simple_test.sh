#!/bin/bash
#
# YAT_CASCHED 极简测试环境构建脚本
#
# 目录职责约定（A7 统一后）：
#   boot_test_scripts/   只放 C 源码（.c），不放任何 shell 脚本、不放编译产物
#   yat_test_scripts/    只放 QEMU / initramfs 编排脚本（.sh）
#
# 本脚本原位于 boot_test_scripts/，A7 迁移到此处。所有产物一律落在
# <内核根>/yat_simple_test_env/ 下，不再污染源码目录（该目录已被 .gitignore 忽略）。

set -e

echo "=== YAT_CASCHED 极简测试环境构建 ==="

# 本脚本位于 <内核根>/yat_test_scripts
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$WORK_DIR/boot_test_scripts"
TEST_DIR="$WORK_DIR/yat_simple_test_env"
INITRAMFS_DIR="$TEST_DIR/initramfs"
BUILD_DIR="$TEST_DIR/build"
IMAGE="$TEST_DIR/yat_simple_test.cpio.gz"

if [ ! -f "$SRC_DIR/yat_simple_test.c" ]; then
    echo "错误: 找不到源码 $SRC_DIR/yat_simple_test.c"
    exit 1
fi

echo "清理旧的测试环境..."
rm -rf "$TEST_DIR"

echo "创建测试目录结构..."
mkdir -p "$INITRAMFS_DIR"/{bin,sbin,etc,proc,sys,dev,tmp,var,lib,usr/bin,usr/sbin}
mkdir -p "$BUILD_DIR"

echo "复制基本的系统文件..."
mkdir -p "$INITRAMFS_DIR"/{proc,sys,tmp,var,run}
chmod 1777 "$INITRAMFS_DIR/tmp"

echo "编译测试程序..."
# 二进制输出到 BUILD_DIR，而不是源码目录：boot_test_scripts/ 只放 C 源码。
if gcc -O2 -static -o "$BUILD_DIR/yat_simple_test" "$SRC_DIR/yat_simple_test.c"; then
    echo "  测试程序编译成功"
    cp "$BUILD_DIR/yat_simple_test" "$INITRAMFS_DIR/bin/"
    chmod +x "$INITRAMFS_DIR/bin/yat_simple_test"
else
    echo "  测试程序编译失败"
    exit 1
fi

echo "创建 init 脚本..."
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh

echo "=== YAT_CASCHED 极简测试环境启动 ==="

mount -t proc none /proc 2>/dev/null || true
mount -t sysfs none /sys 2>/dev/null || true

echo "系统信息:"
echo "内核版本: $(uname -r)"
if [ -f /proc/cpuinfo ]; then
    echo "CPU信息: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d':' -f2)"
    echo "CPU核心数: $(nproc 2>/dev/null || echo '1')"
else
    echo "CPU信息: 不可用"
    echo "CPU核心数: 1"
fi

echo ""
echo "开始 YAT_CASCHED 调度器测试..."
echo "----------------------------------------"

/bin/yat_simple_test

echo "----------------------------------------"
echo "测试完成"
echo ""
echo "输入 'exit' 或按 Ctrl+A 再按 X 结束"

exec /bin/sh
EOF

chmod +x "$INITRAMFS_DIR/init"

echo "添加基本的 shell 命令..."
if command -v busybox >/dev/null 2>&1; then
    echo "  使用 busybox..."
    cp "$(command -v busybox)" "$INITRAMFS_DIR/bin/"
    cd "$INITRAMFS_DIR/bin"
    for cmd in sh ls cat echo mount umount ps kill halt reboot poweroff; do
        ln -sf busybox "$cmd"
    done
else
    echo "  未找到 busybox，复制系统命令（可能不足以启动 shell）..."
    for cmd in /bin/sh /bin/ls /bin/cat /bin/echo /bin/ps /sbin/halt; do
        if [ -f "$cmd" ]; then
            cp "$cmd" "$INITRAMFS_DIR$cmd" 2>/dev/null || true
        fi
    done
fi

echo "创建 cpio 镜像..."
cd "$INITRAMFS_DIR"
find . | cpio -o -H newc 2>/dev/null | gzip > "$IMAGE"

echo ""
echo "=== 构建完成 ==="
echo "测试镜像: $IMAGE"
echo "大小: $(du -h "$IMAGE" | cut -f1)"
echo ""
echo "启动测试:"
echo "  cd $SCRIPT_DIR && ./start_yat_simple_test.sh"
