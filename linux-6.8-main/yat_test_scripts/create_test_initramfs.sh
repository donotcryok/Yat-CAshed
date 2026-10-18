#!/bin/bash
# 创建包含测试程序的 initramfs
#
# 设计说明：
#   早期版本硬编码了 6 个测试程序源文件，但这些源码并不在本仓库中，
#   导致脚本在干净检出上必然失败。现改为：
#     1) 扫描 boot_test_scripts/ 下实际存在的 *.c，逐个编译；
#     2) 只把编译成功的可执行文件打进 initramfs；
#     3) 一个可执行文件都没有时报错退出（避免产出空壳环境）。

set -euo pipefail

KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$KERNEL_DIR"

BOOT_DIR="boot_test_scripts"
OUT_DIR="test_with_programs_initramfs"
OUT_IMAGE="test_with_programs.cpio.gz"

echo "=== 创建 Yat_Casched 测试 initramfs ==="
echo "内核根目录: $KERNEL_DIR"

# ---------------------------------------------------------------
# 1. 检查基础工具
# ---------------------------------------------------------------
for tool in gcc cpio gzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ 缺少依赖: $tool"
        echo "请安装: sudo apt install gcc cpio gzip busybox-static qemu-system-x86"
        exit 1
    fi
done

if [ ! -f /bin/busybox ]; then
    echo "❌ 未找到 /bin/busybox（需要静态版 busybox）"
    echo "请安装: sudo apt install busybox-static"
    exit 1
fi

# ---------------------------------------------------------------
# 2. 编译 boot_test_scripts/ 下实际存在的测试程序
# ---------------------------------------------------------------
echo ""
echo "🔧 编译测试程序..."
programs=()
shopt -s nullglob
for src in "$BOOT_DIR"/*.c; do
    name="$(basename "$src" .c)"
    bin="$BOOT_DIR/$name"
    echo "  - 编译 $name ..."
    if gcc -O2 -static -o "$bin" "$src"; then
        programs+=("$name")
    else
        echo "    ⚠️  $name 编译失败，已跳过"
        rm -f "$bin"
    fi
done
shopt -u nullglob

if [ ${#programs[@]} -eq 0 ]; then
    echo "❌ $BOOT_DIR/ 下没有任何可编译的 *.c，无法生成测试环境"
    echo "   提示：boot_test_scripts/yat_simple_test.c 是当前唯一维护的测试程序"
    exit 1
fi

echo "✅ 可打包程序: ${programs[*]}"

# ---------------------------------------------------------------
# 3. 组装 initramfs 目录树
# ---------------------------------------------------------------
echo ""
echo "📦 组装 initramfs 目录树..."
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"/{bin,sbin,etc,proc,sys,dev,tmp,usr/bin}

cp /bin/busybox "$OUT_DIR/bin/"
chmod +x "$OUT_DIR/bin/busybox"

for name in "${programs[@]}"; do
    cp "$BOOT_DIR/$name" "$OUT_DIR/bin/"
    chmod +x "$OUT_DIR/bin/$name"
done

# ---------------------------------------------------------------
# 4. 生成 init 脚本（菜单按实际打包内容动态生成）
# ---------------------------------------------------------------
{
    echo '#!/bin/busybox sh'
    echo ''
    echo 'echo "=== Yat_Casched 测试环境启动成功 ==="'
    echo 'echo "内核版本: $(busybox uname -r)"'
    echo ''
    echo '/bin/busybox mount -t proc proc /proc'
    echo '/bin/busybox mount -t sysfs sysfs /sys'
    echo '/bin/busybox mount -t devtmpfs devtmpfs /dev 2>/dev/null || true'
    echo ''
    echo '/bin/busybox --install -s'
    echo ''
    echo 'echo ""'
    echo 'echo "📋 可用的测试程序："'
    for name in "${programs[@]}"; do
        echo "echo \"  - $name\""
    done
    echo 'echo ""'
    echo 'echo "基本命令：ps / top / free / dmesg / cat /proc/version"'
    echo 'echo "退出：exit"'
    echo ''
    echo 'exec /bin/busybox sh'
} > "$OUT_DIR/init"
chmod +x "$OUT_DIR/init"

# ---------------------------------------------------------------
# 5. 打包
# ---------------------------------------------------------------
echo ""
echo "🗜️  打包 initramfs..."
(
    cd "$OUT_DIR"
    find . | cpio -o -H newc 2>/dev/null | gzip > "../$OUT_IMAGE"
)

echo ""
echo "✅ 完成: $OUT_IMAGE"
echo ""
echo "启动方式："
echo "  cd $KERNEL_DIR/yat_test_scripts && ./start_test_environment.sh"
echo "  或手工："
echo "  qemu-system-x86_64 -nographic -m 1024 -kernel vmlinux \\"
echo "      -initrd $OUT_IMAGE -append \"console=ttyS0,115200 noapic nolapic acpi=off\" -accel tcg"
