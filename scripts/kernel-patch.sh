#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# kernel-patch.sh —— 补丁包的应用 / 回退 / 往返自检
#
# 用法：
#   scripts/kernel-patch.sh --check      # 仅校验补丁完整性（不触碰内核树）
#   scripts/kernel-patch.sh --apply      # 应用到 linux-6.8-main/
#   scripts/kernel-patch.sh --reverse    # 从 linux-6.8-main/ 回退
#   scripts/kernel-patch.sh --status     # 报告每个补丁是否已应用
#   scripts/kernel-patch.sh --selftest   # 在临时目录做 apply→比对→reverse→比对 往返测试
#
# --selftest 需要一个「上游参照目录」（含未修改的 11 个文件）：
#   scripts/kernel-patch.sh --selftest --upstream /path/to/linux-6.8
# ---------------------------------------------------------------------------
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="${REPO_ROOT}/patches"
KERNEL_DIR="${REPO_ROOT}/linux-6.8-main"
UPSTREAM=""

MODE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --check|--apply|--reverse|--status|--selftest) MODE="${1#--}"; shift ;;
    --upstream) UPSTREAM="$2"; shift 2 ;;
    --dir)      KERNEL_DIR="$2"; shift 2 ;;
    -h|--help)  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数：$1" >&2; exit 1 ;;
  esac
done

die() { echo "错误：$*" >&2; exit 1; }
log() { echo "[kernel-patch] $*"; }

[ -n "$MODE" ] || die "需指定 --check/--apply/--reverse/--status/--selftest"
[ -f "${PATCH_DIR}/series" ] || die "缺少 ${PATCH_DIR}/series"

list_patches() { sed "s|^|${PATCH_DIR}/|" "${PATCH_DIR}/series"; }

verify_checksums() {
  log "校验补丁 SHA256"
  ( cd "$PATCH_DIR" && sha256sum -c --quiet MANIFEST.sha256 ) || die "校验和失败"
  log "校验和 OK ✅"
}

# 每个补丁涉及的文件（用于 --status / --selftest）
files_of() {
  sed -n -E 's/^(\+\+\+ b\/)(.+)$/\2/p' "$1" | sort -u
}

# 补丁中的「新建文件」（--- /dev/null）
new_files_of() {
  awk '/^--- \/dev\/null/ {getline; sub(/^\+\+\+ b\//, ""); print}' "$1"
}

# patch -R 对新建文件只会清空内容而留下 0 字节文件，
# 这里把「回退后为空且不在上游」的文件真正删除。
cleanup_empty_new_files() { # $1 = 工作目录
  local dir="$1" f
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    if [ -f "${dir}/${f}" ] && [ ! -s "${dir}/${f}" ]; then
      rm -f "${dir}/${f}"
      log "  删除回退后残留的空文件：${f}"
    fi
  done < <(
    for p in $(list_patches); do new_files_of "$p"; done | sort -u
  )
}

case "$MODE" in
  check)
    verify_checksums
    n=0
    while read -r p; do
      [ -f "${PATCH_DIR}/${p}" ] || die "series 中的 ${p} 不存在"
      h=$(grep -c '^@@' "${PATCH_DIR}/${p}" || true)
      [ "$h" -gt 0 ] || die "${p} 不含 hunk"
      log "  ${p}: ${h} hunk"
      n=$((n + 1))
    done < "${PATCH_DIR}/series"
    log "共 ${n} 个补丁，全部可读 ✅"
    ;;

  apply)
    verify_checksums
    [ -d "$KERNEL_DIR" ] || die "内核目录不存在：$KERNEL_DIR（先跑 scripts/setup-kernel.sh）"
    cd "$KERNEL_DIR"
    if git rev-parse --git-dir >/dev/null 2>&1; then
      while read -r p; do
        log "git apply -p1 $p"
        git apply -p1 --check "${PATCH_DIR}/${p}" || die "预检失败：$p（可能已应用或源码不匹配）"
        git apply -p1 "${PATCH_DIR}/${p}"
      done < "${PATCH_DIR}/series"
    else
      while read -r p; do
        log "patch -p1 $p"
        patch -p1 -t --dry-run -s < "${PATCH_DIR}/${p}" >/dev/null || die "预检失败：$p"
        patch -p1 -t --no-backup-if-mismatch -s < "${PATCH_DIR}/${p}"
      done < "${PATCH_DIR}/series"
    fi
    log "应用完成 ✅"
    ;;

  reverse)
    [ -d "$KERNEL_DIR" ] || die "内核目录不存在：$KERNEL_DIR"
    cd "$KERNEL_DIR"
    while read -r p; do
      log "回退 $p"
      if git rev-parse --git-dir >/dev/null 2>&1; then
        git apply -R -p1 "${PATCH_DIR}/${p}"
      else
        patch -R -p1 -t --no-backup-if-mismatch -s < "${PATCH_DIR}/${p}"
      fi
    done < <(tac "${PATCH_DIR}/series")
    cleanup_empty_new_files "$KERNEL_DIR"
    log "回退完成 ✅"
    ;;

  status)
    [ -d "$KERNEL_DIR" ] || die "内核目录不存在：$KERNEL_DIR"
    cd "$KERNEL_DIR"
    while read -r p; do
      state="未应用"
      if git rev-parse --git-dir >/dev/null 2>&1; then
        if git apply -R -p1 --check "${PATCH_DIR}/${p}" >/dev/null 2>&1; then
          state="已应用"
        fi
      else
        if patch -R -p1 -t --dry-run -s < "${PATCH_DIR}/${p}" >/dev/null 2>&1; then
          state="已应用"
        fi
      fi
      printf '  %-48s %s\n' "$p" "$state"
    done < "${PATCH_DIR}/series"
    ;;

  selftest)
    verify_checksums
    [ -n "$UPSTREAM" ] || die "--selftest 需要 --upstream <v6.8 源码目录>"
    [ -d "$UPSTREAM" ] || die "上游目录不存在：$UPSTREAM"

    # 只挑选补丁涉及的上游文件，构造最小镜像（整树拷贝太慢）
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    log "构造最小上游镜像：$tmp"

    mapfile -t all_files < <(
      for p in $(list_patches); do files_of "$p"; done | sort -u
    )
    for f in "${all_files[@]}"; do
      f="${f#./}"
      # 新增文件的父目录也要建好，否则 patch 无法创建文件
      mkdir -p "${tmp}/$(dirname "$f")"
      if [ -f "${UPSTREAM}/${f}" ]; then
        cp "${UPSTREAM}/${f}" "${tmp}/${f}"
      fi
    done

    log "正向应用"
    cfg_before="$(find "$tmp" -type f | sort)"
    while read -r p; do
      ( cd "$tmp" && patch -p1 -t --no-backup-if-mismatch -s < "${PATCH_DIR}/${p}" ) \
        || die "正向应用失败：$p"
    done < "${PATCH_DIR}/series"

    log "比对仓库实际文件"
    bad=0
    for f in "${all_files[@]}"; do
      f="${f#./}"
      if [ -f "${KERNEL_DIR}/${f}" ]; then
        diff -q "${tmp}/${f}" "${KERNEL_DIR}/${f}" >/dev/null \
          || { echo "  不一致：${f}" >&2; bad=$((bad + 1)); }
      fi
    done
    [ "$bad" -eq 0 ] || die "正向结果与仓库不一致（${bad} 个文件）"
    log "正向结果与仓库逐字节一致 ✅"

    log "反向回退"
    while read -r p; do
      ( cd "$tmp" && patch -R -p1 -t --no-backup-if-mismatch -s < "${PATCH_DIR}/${p}" ) \
        || die "反向回退失败：$p"
    done < <(tac "${PATCH_DIR}/series")
    cleanup_empty_new_files "$tmp"

    bad=0
    for f in "${all_files[@]}"; do
      f="${f#./}"
      if [ -f "${UPSTREAM}/${f}" ]; then
        diff -q "${tmp}/${f}" "${UPSTREAM}/${f}" >/dev/null \
          || { echo "  回退残留：${f}" >&2; bad=$((bad + 1)); }
      elif [ -e "${tmp}/${f}" ]; then
        echo "  回退残留：${f}" >&2
        bad=$((bad + 1))
      fi
    done
    [ "$bad" -eq 0 ] || die "回退后与上游不一致（${bad} 个文件）"
    log "回退后与上游一致 ✅"
    log "SELFTEST PASS ✅"
    ;;
esac
