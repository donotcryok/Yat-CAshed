#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup-kernel.sh —— 从上游 linux v6.8 重建本项目的内核树（不再入库 1.6GB）
#
# 用法：
#   scripts/setup-kernel.sh                    # 下载 v6.8 tarball 并打补丁到 linux-6.8-main/
#   scripts/setup-kernel.sh --src /path/to/v6.8  # 使用本地已有的 v6.8 源码树（免下载）
#   scripts/setup-kernel.sh --dir linux-6.8-main # 指定目标目录
#   scripts/setup-kernel.sh --build             # 打完补丁后额外编译 vmlinux
#   scripts/setup-kernel.sh --check             # 只做一致性校验，不改动任何文件
#
# 说明：
#   * 目标目录若已存在且非空，脚本只做「打补丁 + 校验」，不会删除你的文件。
#   * 下载使用 git.kernel.org 的 tarball；失败时可手动下载后用 --src 指定。
# ---------------------------------------------------------------------------
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="${REPO_ROOT}/patches"
KERNEL_DIR="${REPO_ROOT}/linux-6.8-main"
SRC=""
BUILD=0
CHECK_ONLY=0
VERSION="v6.8"
TARBALL="linux-6.8.tar.gz"
URL="https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/snapshot/linux-6.8.tar.gz"

die() { echo "错误：$*" >&2; exit 1; }
log() { echo "[setup-kernel] $*"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --src)   SRC="$2"; shift 2 ;;
    --dir)   KERNEL_DIR="$2"; shift 2 ;;
    --build) BUILD=1; shift ;;
    --check) CHECK_ONLY=1; shift ;;
    --version) VERSION="$2"; shift 2 ;;
    --url)   URL="$2"; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "未知参数：$1" ;;
  esac
done

[ -d "$PATCH_DIR" ] || die "补丁目录不存在：$PATCH_DIR（先运行 scripts/gen_kernel_patches.sh）"
[ -f "${PATCH_DIR}/series" ] || die "缺少 ${PATCH_DIR}/series"

# ---------- 1. 校验补丁完整性 ----------
log "校验补丁校验和"
( cd "$PATCH_DIR" && sha256sum -c --quiet MANIFEST.sha256 ) || die "补丁校验和失败"

# ---------- 2. 准备上游源码 ----------
prepare_src() {
  if [ -n "$SRC" ]; then
    [ -d "$SRC" ] || die "--src 目录不存在：$SRC"
    log "使用本地上游源码：$SRC"
    return
  fi

  local cache="${REPO_ROOT}/.cache"
  mkdir -p "$cache"
  if [ ! -f "${cache}/${TARBALL}" ]; then
    log "下载 ${VERSION}（约 210MB，可中断重试）"
    curl -fL --retry 3 --retry-delay 2 -C - \
         -o "${cache}/${TARBALL}.part" "$URL" \
      || die "下载失败。可手动下载后用 --src 指定已解压的源码目录"
    mv "${cache}/${TARBALL}.part" "${cache}/${TARBALL}"
  fi
  if [ ! -d "${cache}/linux-6.8" ]; then
    log "解压 tarball"
    tar -xzf "${cache}/${TARBALL}" -C "$cache"
  fi
  SRC="${cache}/linux-6.8"
  log "上游源码：$SRC"
}

if [ "$CHECK_ONLY" -eq 0 ]; then
  prepare_src
fi

# ---------- 3. 应用到目标目录 ----------
if [ "$CHECK_ONLY" -eq 0 ]; then
  if [ ! -d "$KERNEL_DIR" ]; then
    log "创建内核树（硬链接拷贝，省磁盘）"
    mkdir -p "$(dirname "$KERNEL_DIR")"
    cp -al "$SRC" "$KERNEL_DIR" 2>/dev/null || cp -a "$SRC" "$KERNEL_DIR"
  else
    log "目标已存在，仅增量打补丁：$KERNEL_DIR"
  fi

  log "应用补丁（git apply 优先，回退 patch）"
  cd "$KERNEL_DIR"
  use_git=0
  if command -v git >/dev/null 2>&1 && git apply --check -p1 \
        < "${PATCH_DIR}/$(head -n1 "${PATCH_DIR}/series")" 2>/dev/null; then
    use_git=1
  fi

  if [ "$use_git" -eq 1 ]; then
    while read -r p; do
      log "  git apply $p"
      git apply -p1 "${PATCH_DIR}/${p}" || die "应用失败：$p"
    done < "${PATCH_DIR}/series"
  else
    while read -r p; do
      log "  patch -p1 $p"
      patch -p1 --no-backup-if-mismatch -s < "${PATCH_DIR}/${p}" \
        || die "应用失败：$p（可能是源码版本不匹配，需要 ${VERSION}）"
    done < "${PATCH_DIR}/series"
  fi
fi

# ---------- 4. 一致性校验 ----------
log "校验关键文件是否齐备"
missing=0
for f in kernel/sched/yat_casched.c \
         include/linux/sched/yat_casched.h \
         initramfs/init \
         boot_test_scripts/yat_simple_test.c \
         yat_test_scripts/start_qemu_yat_test.sh; do
  if [ ! -f "${KERNEL_DIR}/${f}" ]; then
    echo "  缺失：${f}" >&2
    missing=$((missing + 1))
  fi
done
[ "$missing" -eq 0 ] || die "补丁应用后仍缺 ${missing} 个文件"
log "关键文件齐备 ✅"

# 集成点抽查
grep -q 'SCHED_CLASS_YAT_CASCHED' "${KERNEL_DIR}/init/Kconfig" \
  || die "init/Kconfig 缺少 CONFIG_SCHED_CLASS_YAT_CASCHED"
grep -q 'yat_casched.o' "${KERNEL_DIR}/kernel/sched/Makefile" \
  || die "kernel/sched/Makefile 缺少 obj 规则"
grep -q '__yat_casched_sched_class' "${KERNEL_DIR}/include/asm-generic/vmlinux.lds.h" \
  || die "vmlinux.lds.h 缺少链接段"
grep -q 'DEFINE_SCHED_CLASS(yat_casched)' "${KERNEL_DIR}/kernel/sched/yat_casched.c" \
  || die "yat_casched.c 缺少 DEFINE_SCHED_CLASS"
log "集成点抽查通过 ✅"

# ---------- 5. 可选编译 ----------
if [ "$BUILD" -eq 1 ]; then
  log "编译 vmlinux（tinyconfig + 必要选项）"
  cd "$KERNEL_DIR"
  make tinyconfig

  cfg_script=(
    "scripts/config --enable SMP"
    "scripts/config --enable SCHED_SMT"
    "scripts/config --enable SCHED_MC"
    "scripts/config --enable SCHED_CLASS_YAT_CASCHED"
    "scripts/config --enable PRINTK"
    "scripts/config --enable BLK_DEV_INITRD"
    "scripts/config --enable RD_GZIP"
  )
  for c in "${cfg_script[@]}"; do eval "$c"; done
  make olddefconfig
  make -j"$(nproc)" vmlinux
  ls -l vmlinux
fi

log "完成 ✅"
log "提示：日常开发可直接在 ${KERNEL_DIR} 内 make/qemu；"
log "      确认无新增改动后，用 scripts/gen_kernel_patches.sh 刷新补丁。"
