#!/usr/bin/env python3
"""检查 Markdown 文档中的相对链接是否指向真实存在的文件。

设计原则：
- 只检查「本项目自己维护」的文档，避免扫描整棵 linux-6.8-main 内核树（数万文件）。
- 忽略外链（http/https/mailto）、纯锚点（#xxx）以及代码块内的示例链接。
- 链接目标允许携带 `#anchor`，但锚点本身不做校验（Markdown 锚点规则因渲染器而异）。
- 退出码：0 = 全部有效；1 = 存在失效链接。
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


def check_file(md_file: Path) -> list[tuple[int, str, str]]:
    """返回 [(行号, 原始目标, 解析后的路径)]，仅包含失效项。"""
    try:
        text = md_file.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:  # pragma: no cover - 防御性
        return [(0, str(md_file), f"读取失败: {exc}")]

    broken: list[tuple[int, str, str]] = []
    for lineno, line in strip_code_blocks(text.splitlines()):
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
            resolved = (md_file.parent / clean).resolve()
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
