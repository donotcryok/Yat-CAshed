#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# gen_kernel_patches.sh —— 从「上游 linux v6.8」+「本仓库 linux-6.8-main/」
# 生成一组可独立应用的补丁（patches/），用于替代 1.6GB 的内核整树入库。
#
# 用法：
#   scripts/gen_kernel_patches.sh --upstream /path/to/linux-v6.8
#   scripts/gen_kernel_patches.sh --upstream /tmp/up --out patches
#
# 要求：--upstream 目录中必须包含与 linux-6.8-main/ 同名的「未修改」文件，
#       即最小集为下面 UPSTREAM_FILES 列出的 11 个文件（可从
#       scripts/setup-kernel.sh 下载的 v6.8 源码树中拷贝，或直接用整棵源码树）。
#
# 产物：
#   patches/series                          补丁应用顺序
#   patches/000N-*.patch                    具体补丁
#   patches/MANIFEST.sha256                 补丁校验和
#   patches/README.md                       使用说明（由本脚本刷新）
# ---------------------------------------------------------------------------
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_DIR="${REPO_ROOT}/linux-6.8-main"
UPSTREAM=""
OUT="${REPO_ROOT}/patches"

# 被修改的上游文件（与 v6.8 不同）
UPSTREAM_FILES=(
  .gitignore
  arch/x86/realmode/init.c
  include/asm-generic/vmlinux.lds.h
  include/linux/sched.h
  include/uapi/linux/sched.h
  init/Kconfig
  init/init_task.c
  init/main.c
  kernel/sched/Makefile
  kernel/sched/core.c
  kernel/sched/sched.h
)

# 新增文件（上游不存在）。二进制/生成物不入补丁。
#
# 目录约定（A7）：boot_test_scripts/ 只放 C 源码，脚本一律在 yat_test_scripts/。
NEW_FILES=(
  include/linux/sched/yat_casched.h
  include/trace/events/yat_casched.h
  kernel/sched/yat_casched.c
  initramfs/init
  initramfs_fixed/init
  yat_simple_test_env/initramfs/init
  boot_test_scripts/README.md
  boot_test_scripts/SIMPLE_TEST_README.md
  boot_test_scripts/yat_bench.c
  boot_test_scripts/yat_simple_test.c
  yat_test_scripts/README.md
  yat_test_scripts/build_yat_simple_test.sh
  yat_test_scripts/compare_cfs_vs_yat.sh
  yat_test_scripts/create_test_initramfs.sh
  yat_test_scripts/manual_test_guide.sh
  yat_test_scripts/quick_test_guide.sh
  yat_test_scripts/start_qemu_simple.sh
  yat_test_scripts/start_qemu_yat_test.sh
  yat_test_scripts/start_test_environment.sh
  yat_test_scripts/start_yat_simple_test.sh
  test_visualization/README.md
  test_visualization/debug_scheduler.c
  test_visualization/performance_test.c
  test_visualization/visualize_results.py
  test_visualization/performance_results.csv
  test_visualization/img/performance_summary.md
  Yat_Casched开发文档.md
  yat_casched优化记录.md
)

die() { echo "错误：$*" >&2; exit 1; }
log() { echo "[patches] $*"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --upstream) UPSTREAM="$2"; shift 2 ;;
    --out)      OUT="$2"; shift 2 ;;
    -h|--help)  sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "未知参数：$1" ;;
  esac
done

[ -n "$UPSTREAM" ] || die "必须指定 --upstream（v6.8 源码目录）"
[ -d "$UPSTREAM" ] || die "上游目录不存在：$UPSTREAM"
[ -d "$KERNEL_DIR" ] || die "本仓库内核目录不存在：$KERNEL_DIR"

# 校验上游参照文件齐全
missing=0
for f in "${UPSTREAM_FILES[@]}"; do
  if [ ! -f "${UPSTREAM}/${f}" ]; then
    echo "  缺失上游参照：${f}" >&2
    missing=$((missing + 1))
  fi
done
[ "$missing" -eq 0 ] || die "上游目录不完整（缺 ${missing} 个文件），请指向完整的 v6.8 源码树"

rm -rf "$OUT"
mkdir -p "$OUT"

emit_hunk() { # $1=旧文件路径(或/dev/null) $2=新文件路径
  local old="$1" new="$2"
  if [ "$old" = "/dev/null" ]; then
    # 新建文件：--- 必须是 /dev/null，否则 patch -R 无法删除该文件
    diff -u --label "/dev/null" --label "b/${new}" \
         /dev/null "${KERNEL_DIR}/${new}" || true
  else
    diff -u --label "a/${new}" --label "b/${new}" \
         "${UPSTREAM}/${old}" "${KERNEL_DIR}/${new}" || true
  fi
}

make_patch() { # $1=文件名 $2=标题 $3...=路径列表（前缀 N: 表示新文件）
  local name="$1" subject="$2"; shift 2
  local out="${OUT}/${name}"
  {
    echo "From: Yat-CAShed <noreply@example.com>"
    echo "Subject: [PATCH] ${subject}"
    echo "Upstream: linux v6.8 (torvalds)"
    echo ""
    local p
    for p in "$@"; do
      if [ "${p#N:}" != "$p" ]; then
        emit_hunk /dev/null "${p#N:}"
      else
        emit_hunk "$p" "$p"
      fi
    done
  } > "$out"
  # 去掉空补丁
  if ! grep -q '^@@' "$out"; then
    rm -f "$out"
    log "跳过空补丁 ${name}"
  else
    log "生成 ${name}（$(grep -c '^@@' "$out") 个 hunk）"
  fi
}

make_patch 0001-sched-yat-casched-core.patch \
  "sched: add YAT_CASCHED cache-aware scheduling class (core)" \
  N:include/linux/sched/yat_casched.h \
  N:include/trace/events/yat_casched.h \
  N:kernel/sched/yat_casched.c

make_patch 0002-sched-yat-casched-integration.patch \
  "sched: wire YAT_CASCHED into core, uapi, init and linker sections" \
  include/asm-generic/vmlinux.lds.h \
  include/linux/sched.h \
  include/uapi/linux/sched.h \
  init/Kconfig \
  init/init_task.c \
  init/main.c \
  kernel/sched/Makefile \
  kernel/sched/core.c \
  kernel/sched/sched.h

make_patch 0003-x86-realmode-tolerate-missing-trampoline.patch \
  "x86/realmode: degrade gracefully when trampoline allocation fails" \
  arch/x86/realmode/init.c

make_patch 0004-kernel-gitignore.patch \
  "kbuild: fix .gitignore case-folding and Yat test artifacts" \
  .gitignore

make_patch 0005-test-env.patch \
  "test: add Yat QEMU boot/test environments and CFS-vs-Yat benchmark" \
  N:initramfs/init \
  N:initramfs_fixed/init \
  N:yat_simple_test_env/initramfs/init \
  N:boot_test_scripts/README.md \
  N:boot_test_scripts/SIMPLE_TEST_README.md \
  N:boot_test_scripts/yat_bench.c \
  N:boot_test_scripts/yat_simple_test.c \
  N:yat_test_scripts/README.md \
  N:yat_test_scripts/build_yat_simple_test.sh \
  N:yat_test_scripts/compare_cfs_vs_yat.sh \
  N:yat_test_scripts/create_test_initramfs.sh \
  N:yat_test_scripts/manual_test_guide.sh \
  N:yat_test_scripts/quick_test_guide.sh \
  N:yat_test_scripts/start_qemu_simple.sh \
  N:yat_test_scripts/start_qemu_yat_test.sh \
  N:yat_test_scripts/start_test_environment.sh \
  N:yat_test_scripts/start_yat_simple_test.sh \
  N:test_visualization/README.md \
  N:test_visualization/debug_scheduler.c \
  N:test_visualization/performance_test.c \
  N:test_visualization/visualize_results.py \
  N:test_visualization/performance_results.csv \
  N:test_visualization/img/performance_summary.md

make_patch 0006-docs.patch \
  "docs: add Yat_Casched design and optimization notes" \
  N:Yat_Casched开发文档.md \
  N:yat_casched优化记录.md

# series（按序）
ls "$OUT"/0*.patch | xargs -n1 basename | sort > "${OUT}/series"

# 校验和
( cd "$OUT" && sha256sum $(cat series) > MANIFEST.sha256 )

# README
cat > "${OUT}/README.md" <<'EOF'
# 内核补丁包（Yat-CASCHED）

本目录把 `linux-6.8-main/` 中相对上游 **linux v6.8** 的全部改动固化为补丁，
使 83k 文件 / 1.6GB 的内核整树**不必入库**。

## 文件

| 文件 | 内容 |
| --- | --- |
| `series` | 应用顺序（6 个补丁） |
| `0001-sched-yat-casched-core.patch` | 新增 `kernel/sched/yat_casched.c`（含 A6 的 tracepoint 与 debugfs）与 `include/linux/sched/yat_casched.h`、`include/trace/events/yat_casched.h` |
| `0002-sched-yat-casched-integration.patch` | 调度类接入：`core.c` / `sched.h` / `uapi/sched.h` / `init/main.c` / `init/Kconfig` / `init/init_task.c` / `kernel/sched/Makefile` / `vmlinux.lds.h` |
| `0003-x86-realmode-tolerate-missing-trampoline.patch` | `arch/x86/realmode/init.c`：trampoline 分配失败时降级而非 panic |
| `0004-kernel-gitignore.patch` | 内核树 `.gitignore`：修复 `*.s` 误伤 `.S`、`tags` 误伤源码目录，追加测试产物（含 `test_with_programs_build/`、`yat_test_scripts/results/`） |
| `0005-test-env.patch` | `boot_test_scripts/`（**只含 C 源码**：`yat_simple_test.c`、`yat_bench.c`）、`yat_test_scripts/`（**只含脚本**：含 `compare_cfs_vs_yat.sh`）、`test_visualization/`、三个 `initramfs/init` |
| `0006-docs.patch` | `Yat_Casched开发文档.md`、`yat_casched优化记录.md` |

> 二进制产物（`test_visualization/img/*.png`）与编译产物（`*.o`、`vmlinux`）
> **不入补丁**，它们是可再生成的实验结果，保存在仓库
> `docs/assets/` 或 CI 制品中。

## 应用

```bash
# 方式一：git（推荐，能保留上下文校验）
git clone --depth 1 --branch v6.8 \
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git \
  linux-6.8-main
cd linux-6.8-main
git apply -p1 ../patches/0001-sched-yat-casched-core.patch
# ... 或一次性：
git apply -p1 $(sed 's|^|../patches/|' ../patches/series)

# 方式二：patch
for p in $(cat ../patches/series); do patch -p1 < "../patches/$p"; done
```

## 回退

```bash
for p in $(tac ../patches/series); do
  git apply -R -p1 "../patches/$p"     # 或 patch -R -p1 < "../patches/$p"
done
```

## 校验

```bash
( cd ../patches && sha256sum -c MANIFEST.sha256 )
```

## 一键准备

见仓库根目录 `scripts/setup-kernel.sh`（下载 v6.8 → 应用补丁 → 可选编译）。
EOF

log "完成：${OUT}"
log "补丁数：$(wc -l < "${OUT}/series")，总体积：$(du -sh "$OUT" | cut -f1)"
