#!/usr/bin/env bash
# ============================================================
# Yat-Casched 内核符号一致性检查（静态分析）
# ------------------------------------------------------------
# 目标：在不编译、不改动源码的前提下，比对
#   include/linux/sched/yat_casched.h  的「声明」
# 与
#   kernel/sched/yat_casched.c         的「定义」
# 是否一一对应，用于尽早发现
#   - 声明了却无实现（链接期 undefined reference 隐患）
#   - 实现了却无声明（外部链接接口泄漏 / 头文件未同步）
#
# 说明：
#   1. 合并冲突已按路线 A 解决，脚本不再对冲突双方取并集。
#   2. `static` 函数属文件内私有实现，**不计入**接口泄漏，
#      仅作为信息统计展示，避免噪声淹没真正的问题。
#   3. 以 ';' 结尾的原型声明行不算「定义」。
#   4. 三类问题（声明未实现 / 接口泄漏 / 链接属性不一致）均为阻断项，
#      脚本不再保留任何白名单。
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="${ROOT}/linux-6.8-main/include/linux/sched/yat_casched.h"
IMPL="${ROOT}/linux-6.8-main/kernel/sched/yat_casched.c"

fail=0

echo "=========================================="
echo " Yat-Casched 符号一致性检查"
echo "=========================================="

for f in "$HEADER" "$IMPL"; do
  if [ ! -f "$f" ]; then
    echo "❌ 找不到文件: $f"
    exit 2
  fi
done

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

# 从一行源码里抽取第一个 yat_* 函数名
extract_name() { sed -E 's/.*\b(yat_[a-zA-Z0-9_]+)\s*\(.*/\1/'; }

# ---------- 1) 头文件：对外声明（函数原型）----------
grep -oE '\byat_[a-zA-Z0-9_]+\s*\(' "$HEADER" \
  | sed -E 's/[[:space:]]*\($//' \
  | sort -u > "$tmpdir/declared.txt"

# ---------- 2) 实现：函数定义（排除仅原型行）----------
# 定义行特征：顶格（列 0）起始、含 yat_* 名字、且**不以 ';' 结尾**
DEF_RE='^[A-Za-z_][A-Za-z0-9_ \t\*]*\byat_[a-zA-Z0-9_]+\s*\('

# 2a) 外部链接（非 static）定义 —— 应出现在头文件中
grep -E "$DEF_RE" "$IMPL" \
  | grep -v ';[[:space:]]*$' \
  | grep -v '^static[[:space:]]' \
  | extract_name | sort -u > "$tmpdir/defined_ext.txt"

# 2b) static 文件内私有定义 —— 允许不出现在头文件中
grep -E '^static[[:space:]].*\byat_[a-zA-Z0-9_]+\s*\(' "$IMPL" \
  | grep -v ';[[:space:]]*$' \
  | extract_name | sort -u > "$tmpdir/defined_static.txt"

# 2c) 全体定义（供「声明未实现」判定）
sort -u "$tmpdir/defined_ext.txt" "$tmpdir/defined_static.txt" > "$tmpdir/defined_all.txt"

declared_cnt=$(wc -l < "$tmpdir/declared.txt" | tr -d ' ')
ext_cnt=$(wc -l < "$tmpdir/defined_ext.txt" | tr -d ' ')
static_cnt=$(wc -l < "$tmpdir/defined_static.txt" | tr -d ' ')

echo
echo "头文件声明函数:       ${declared_cnt}"
echo "源码定义（非 static）: ${ext_cnt}"
echo "源码定义（static）:    ${static_cnt}   （文件内私有，不计入接口泄漏）"

# ---------- 3) 声明但无实现 ----------
comm -23 "$tmpdir/declared.txt" "$tmpdir/defined_all.txt" > "$tmpdir/missing.txt"
# ---------- 4) 非 static 实现但无声明（真正的接口泄漏）----------
comm -13 "$tmpdir/declared.txt" "$tmpdir/defined_ext.txt" > "$tmpdir/leak.txt"

# ---------- 4b) static 声明 ↔ 非 static 定义（链接属性不一致，编译级缺陷）----------
# 前置原型形如 `static void yat_x(...);`，而定义处缺少 static
grep -E '^static[[:space:]].*\byat_[a-zA-Z0-9_]+\s*\(' "$IMPL" \
  | grep ';[[:space:]]*$' \
  | extract_name | sort -u > "$tmpdir/proto_static.txt"
comm -12 "$tmpdir/proto_static.txt" "$tmpdir/defined_ext.txt" > "$tmpdir/linkage_bad.txt"

# 白名单已全部清空：冲突按路线 A 解决后，
#   - yat_global_pool_*（7 个）已从 yat_casched.h 删除（死接口，无调用点），
#   - yat_create_job_for_task / yat_complete_task_job / yat_create_test_dag 已补声明，
#   - yat_ajlr_dequeue_task 已补 static（CONFLICT_ANALYSIS.md §6.1）。
# 此后任何「声明未实现 / 接口泄漏 / 链接属性不一致」一律阻断。
KNOWN_MISSING_RE='^$'
KNOWN_LEAK_RE='^$'
KNOWN_LINKAGE_RE='^$'

grep -vE "$KNOWN_MISSING_RE" "$tmpdir/missing.txt" > "$tmpdir/missing_unexpected.txt" || true
grep -E  "$KNOWN_MISSING_RE" "$tmpdir/missing.txt" > "$tmpdir/missing_known.txt"      || true

grep -vE "$KNOWN_LEAK_RE" "$tmpdir/leak.txt" > "$tmpdir/leak_unexpected.txt" || true
grep -E  "$KNOWN_LEAK_RE" "$tmpdir/leak.txt" > "$tmpdir/leak_known.txt"      || true

grep -vE "$KNOWN_LINKAGE_RE" "$tmpdir/linkage_bad.txt" > "$tmpdir/linkage_unexpected.txt" || true
grep -E  "$KNOWN_LINKAGE_RE" "$tmpdir/linkage_bad.txt" > "$tmpdir/linkage_known.txt"      || true

echo
echo "[A] 头文件已声明但未找到实现（缺失定义）:"
if [ -s "$tmpdir/missing_unexpected.txt" ]; then
  echo "   ❌ 非预期缺失（需修正）:"
  sed 's/^/     ❌ /' "$tmpdir/missing_unexpected.txt"
  fail=1
else
  echo "     ✅ 无非预期缺失"
fi
if [ ! -s "$tmpdir/missing.txt" ]; then
  echo "     ✅ 无"
fi

echo
echo "[B] 已实现但头文件未声明（非 static，接口泄漏）:"
if [ -s "$tmpdir/leak_unexpected.txt" ]; then
  echo "   ❌ 非预期泄漏（需修正：补声明或改 static）:"
  sed 's/^/     ❌ /' "$tmpdir/leak_unexpected.txt"
  fail=1
else
  echo "     ✅ 无非预期泄漏"
fi

echo
echo "[C] static 声明 ↔ 非 static 定义（链接属性不一致）:"
if [ -s "$tmpdir/linkage_unexpected.txt" ]; then
  echo "   ❌ 非预期不一致（需修正：补 static 或去掉原型前的 static）:"
  sed 's/^/     ❌ /' "$tmpdir/linkage_unexpected.txt"
  fail=1
else
  echo "     ✅ 无非预期不一致"
fi

# ---------- 5) 冲突标记存在性检查（阻断）----------
echo
if grep -qE '^(<<<<<<< |>>>>>>> )' "$IMPL" 2>/dev/null; then
  echo "❌ $IMPL 含未解决的合并冲突标记（该文件不应再有冲突）。"
  fail=1
else
  echo "✅ 源码无合并冲突标记"
fi

echo
echo "=========================================="
if [ "$fail" -eq 0 ]; then
  echo " ✅ 符号一致性检查通过"
else
  echo " ❌ 存在阻断性问题（见 [A]/[B] ❌ 项），请修正"
fi
echo "=========================================="
exit "$fail"
