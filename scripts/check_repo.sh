#!/usr/bin/env bash
# Yat-CAShed 仓库健康检查
# 由顶层 `make check` 与 CI 调用。发现阻断性问题时以非零码退出。
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0

echo "=========================================="
echo " Yat-CAShed 仓库健康检查"
echo "=========================================="

# 1) 合并冲突标记守卫（仅检查项目自身文件，跳过完整内核树 / 第三方 jar）
echo
echo "[1/3] 检查合并冲突标记..."
# 注：yat_casched.c 的 10 处冲突已按「路线 A」解决，白名单已移除——
# 此后任何冲突标记（含该文件）一律阻断。
CONFLICT_HITS=$(grep -rInE '^(<<<<<<< |>>>>>>> )' \
    --include='*.c' --include='*.h' --include='*.java' \
    --include='*.md' --include='*.sh' --include='*.tex' --include='*.py' \
    --include='Makefile' . 2>/dev/null \
    | grep -v '/linux-6.8-main/tools/' || true)

if [ -n "$CONFLICT_HITS" ]; then
    echo "  ❌ 发现未解决的合并冲突标记："
    echo "$CONFLICT_HITS" | sed 's/^/     /'
    fail=1
else
    echo "  ✅ 未发现冲突标记"
fi

# 2) 空源文件检查（仅项目自身范围）
echo
echo "[2/3] 检查空源文件..."
EMPTY=$(find . -type f -empty \
    -not -path './.git/*' \
    -not -path './linux-6.8-main/*' \
    2>/dev/null || true)
if [ -n "$EMPTY" ]; then
    echo "  ⚠️  发现空文件："
    echo "$EMPTY" | sed 's/^/     /'
else
    echo "  ✅ 未发现空文件"
fi

# 3) 关键文件存在性
echo
echo "[3/3] 检查关键文件..."
REQUIRED=(
    "README.md"
    "docs/ARCHITECTURE.md"
    "docs/CONFLICT_ANALYSIS.md"
    "docs/ROADMAP.md"
    "linux-6.8-main/kernel/sched/yat_casched.c"
    "linux-6.8-main/include/linux/sched/yat_casched.h"
    "simulator/AlgorithmComparisonExperiment.java"
)
for f in "${REQUIRED[@]}"; do
    if [ -e "$f" ]; then
        echo "  ✅ $f"
    else
        echo "  ❌ 缺失：$f"
        fail=1
    fi
done

echo
echo "=========================================="
if [ "$fail" -eq 0 ]; then
    echo " ✅ 健康检查通过"
else
    echo " ❌ 健康检查发现问题（见上）"
fi
echo "=========================================="
exit "$fail"
