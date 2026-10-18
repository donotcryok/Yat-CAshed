#!/usr/bin/env python3
"""检查 Markdown 文档中的相对链接是否指向真实存在的文件。

设计原则：
- 只检查「本项目自己维护」的文档，避免扫描整棵 linux-6.8-main 内核树（数万文件）。
- 忽略外链（http/https/mailto）、纯锚点（#xxx）以及代码块内的示例链接。
- 链接目标允许携带 `#anchor`，但锚点本身不做校验（Markdown 锚点规则因渲染器而异）。
- 退出码：0 = 全部有效；1 = 存在失效链接。

归档文档的链接基准
------------------
`docs/archive/` 下的文档是**历史快照**，正文必须保持原样（不修改、不回填），
但它们内部的相对链接是以**归档前的原始位置**为基准写的。直接把归档件按新位置
解析会产生假阳性——链接本身没错，是基准变了。

因此这里不"跳过"归档件，而是用 `LINK_BASE_OVERRIDES` 把基准换回原目录：
链接仍然照常校验（目标若真的被删了依然会报错），只是解析起点变了。
原路径与基准的对应关系同时登记在 `docs/archive/README.md` 中，两处保持一致。
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent

# 本项目自己维护的 Markdown 范围（相对仓库根目录）
EXPLICIT_GLOBS = [
    "*.md",
    "docs/**/*.md",
    "report/**/*.md",
    "simulator/**/*.md",
    "linux-6.8-main/*.md",
    "linux-6.8-main/boot_test_scripts/*.md",
    "linux-6.8-main/yat_test_scripts/*.md",
    "linux-6.8-main/test_visualization/*.md",
]

# 生成物/依赖目录，直接跳过
SKIP_DIRS = {".git", "classes", "out", "result", "node_modules", "__pycache__", ".venv"}

# 归档件的链接基准覆盖：归档文件（相对仓库根） -> 归档前所在的目录（相对仓库根）
# 归档正文不回填，因此按原目录解析才反映"当时这些链接指向哪里"。
LINK_BASE_OVERRIDES = {
    "docs/archive/Yat-CAShed-初赛版实现文档.md": "linux-6.8-main",
    "docs/archive/Yat-CAShed-决赛版实现文档.md": "report/paper",
}

# 归档件开头有一段**新增的**归档 banner（不是原文），它的链接是按新位置写的。
# 该标记之后才切换到"原目录"基准。标记前的行一律按文件实际位置解析。
ARCHIVE_BANNER_END = "<!-- /archive-banner -->"

LINK_RE = re.compile(r"!?\[[^\]]*\]\(\s*(<[^>]+>|[^)\s]+)(?:\s+['\"][^'\"]*['\"])?\s*\)")
FENCE_RE = re.compile(r"^```|^~~~")
INLINE_CODE_RE = re.compile(r"`[^`]*`")
IGNORED_PREFIXES = ("http://", "https://", "mailto:", "tel:", "ftp://", "#", "data:")


def iter_markdown_files() -> list[Path]:
    seen: set[Path] = set()
    files: list[Path] = []
    for pattern in EXPLICIT_GLOBS:
        for path in sorted(ROOT.glob(pattern)):
            if not path.is_file() or path.suffix.lower() != ".md":
                continue
            if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
                continue
            if path not in seen:
                seen.add(path)
                files.append(path)
    return files


def strip_code_blocks(lines: list[str]) -> list[tuple[int, str]]:
    """去掉围栏代码块，返回 (行号, 内容) 列表。"""
    result: list[tuple[int, str]] = []
    in_fence = False
    for lineno, line in enumerate(lines, start=1):
        if FENCE_RE.match(line.strip()):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        result.append((lineno, INLINE_CODE_RE.sub("", line)))
    return result


def link_bases(md_file: Path, lines: list[str]) -> tuple[Path, Path, int]:
    """返回 (banner 基准, 正文基准, 切换行号)。

    非归档件两者相同、切换行号为 0（永不切换）。
    归档件的 banner 用文件当前位置，正文用归档前目录；
    若文件里没有 banner 结束标记，则全文按归档前目录解析。
    """
    own = md_file.parent
    rel = md_file.relative_to(ROOT).as_posix()
    override = LINK_BASE_OVERRIDES.get(rel)
    if not override:
        return own, own, 0
    archive_base = (ROOT / override).resolve()
    for idx, line in enumerate(lines, start=1):
        if ARCHIVE_BANNER_END in line:
            return own, archive_base, idx
    return archive_base, archive_base, 0


def check_file(md_file: Path) -> list[tuple[int, str, str]]:
    """返回 [(行号, 原始目标, 解析后的路径)]，仅包含失效项。"""
    try:
        text = md_file.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:  # pragma: no cover - 防御性
        return [(0, str(md_file), f"读取失败: {exc}")]

    raw_lines = text.splitlines()
    banner_base, body_base, switch_at = link_bases(md_file, raw_lines)
    broken: list[tuple[int, str, str]] = []
    for lineno, line in strip_code_blocks(raw_lines):
        base = banner_base if (switch_at and lineno <= switch_at) else body_base
        for match in LINK_RE.finditer(line):
            target = match.group(1).strip()
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1].strip()
            if not target or target.startswith(IGNORED_PREFIXES):
                continue
            # 去掉锚点与查询串
            clean = target.split("#", 1)[0].split("?", 1)[0]
            if not clean:
                continue
            clean = unquote(clean)
            if clean.startswith("/"):
                continue  # 站点绝对路径，不适用于仓库内相对校验
            resolved = (base / clean).resolve()
            if not resolved.exists():
                broken.append((lineno, target, clean))
    return broken


def main() -> int:
    files = iter_markdown_files()
    if not files:
        print("未找到任何待检查的 Markdown 文件。")
        return 1

    total_links = 0
    total_broken = 0
    for md_file in files:
        broken = check_file(md_file)
        if broken:
            rel = md_file.relative_to(ROOT)
            for lineno, raw, clean in broken:
                print(f"{rel}:{lineno}: 失效链接 -> {raw}")
                total_broken += 1

    # 统计链接数仅用于提示（不重复解析，做轻量估算）
    for md_file in files:
        try:
            text = md_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for _, line in strip_code_blocks(text.splitlines()):
            for match in LINK_RE.finditer(line):
                target = match.group(1).strip().strip("<>")
                if target and not target.startswith(IGNORED_PREFIXES):
                    total_links += 1

    print(f"\n检查完成：{len(files)} 个 Markdown 文件，{total_links} 条链接，失效 {total_broken} 条。")
    return 1 if total_broken else 0


if __name__ == "__main__":
    sys.exit(main())
