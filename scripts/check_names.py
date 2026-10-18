#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""项目名拼写一致性检查（`make name-check`）。

问题背景
--------
仓库里历史上出现过 6 种项目名写法：

    Yat-CAShed    Yat_CAShed    Yat_Casched      name-check:ignore
    Yat-CASCHED   Yat_CAshed    yat_CAShed       name-check:ignore

散文层混用不会让程序出错，所以一直没人修；但它会让人分不清
"这是同一个项目还是两个"，也让搜索失效（搜 `Yat-CAShed` 漏掉一半）。

规范名
------
`Yat-CAShed`（Yet Another Task CAShed / Cache-Aware Scheduler）。
判定依据不是"哪个出现得多"，而是**哪一个与外部不可变标识绑定**：

  * git remote         git@github.com:donotcryok/Yat-CAShed.git
  * CI badge URL       github.com/mafuquan/Yat-CAShed/actions/...
  * 论文标题           \\title{Yat-CAShed: ...}（report/原理.tex）
  * 仓库根目录名       Yat-CAShed/

这三种外部引用（URL / 已发布的论文标题 / 目录名）一旦改动就断链或失真，
因此它们决定了规范名的唯一合理取值。`Yat-Casched` 只出现在本仓库自己写的  name-check:ignore
约定文字里，没有外部绑定，属于"可以改"的一方。  name-check:ignore

哪些地方**不**检查
------------------
1. `docs/archive/`：归档文档是**历史快照**，正文必须保持原样，
   改了就不再是"当时那份文档"了。归档前的名字在归档 README 中登记。
2. `report/`：论文、报告、参考材料与早期草稿。这些是**交付物/写作素材**，
   其中的写法应当随论文定稿统一，而不是被工程门禁反向约束。
   （论文标题本身就用 `Yat-CAShed`，与规范名一致。）
3. `patches/`：由 `scripts/gen_kernel_patches.sh` 生成，属于产物；
   其内容随源文件生成，检查它没有意义，重生成即可。
4. 二进制与压缩产物：`.pdf` `.pptx` `.docx` `.jar` `.png` 等。
5. `linux-6.8-main/` 下的**上游内核源码**：我们不改上游文件的措辞。
   只检查我们自己新增的 yat_* 文件与测试目录。

白名单（行级）
--------------
以下情形**不算违规**，因为其中的写法是被引用的字面量，不是叙述：

  * 出现在 URL 里（`github.com/.../Yat-CAShed`）；
  * 出现在**已存在的文件名**里（`Yat-CAShed.tex`、`Yat-CAShed.pdf`、
    `yat_CAshed.pdf`）；
  * 该行含显式标记 `name-check:ignore`。用于「必须列出错误写法才说得清」的
    规则说明行——例如 `CONTRIBUTING.md` §6 的命名规则。**这是逃生舱，不是常规手段**：
    每次使用都应能在 code review 中解释得清，否则就是真的违规。

退出码
------
0 = 全部合规；1 = 发现违规（逐条打印 `文件:行: 违规写法  ->  上下文`）。
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# 规范名
CANONICAL = "Yat-CAShed"

# 需要规整的写法（规范名本身不在其中）。
#
# 本元组必须逐字写出每一种错误拼法，否则检查器认不出它们 —— 这些
# 字面量本身就是「规则的定义」，不是需要被规整的散文。因此整块加
# 行级豁免标记：这是 `LINE_ALLOW_SUBSTRINGS` 里 `name-check:ignore`
# 被设计出来的场景（与 CONTRIBUTING.md §6 的命名规则表同理）。
VARIANTS = (  # name-check:ignore
    "Yat_CAShed",       # name-check:ignore
    "Yat_Casched",      # name-check:ignore
    "Yat-CASCHED",      # name-check:ignore
    "Yat_CAshed",       # name-check:ignore
    "yat_CAShed",       # name-check:ignore
    "Yat-Casched",      # name-check:ignore
    "YAT_CASHED",       # name-check:ignore
)

VARIANT_RE = re.compile("|".join(re.escape(v) for v in VARIANTS))

# 整目录豁免
EXEMPT_DIRS = (
    "docs/archive/",
    "report/",
    "patches/",
    ".git/",
)

# `linux-6.8-main/` 下只查我们自己新增的东西（其余是上游源码）
KERNEL_ALLOW_PREFIXES = (
    "linux-6.8-main/boot_test_scripts/",
    "linux-6.8-main/yat_test_scripts/",
    "linux-6.8-main/test_visualization/",
    "linux-6.8-main/yat_simple_test_env/",
    "linux-6.8-main/initramfs/",
    "linux-6.8-main/initramfs_fixed/",
)
KERNEL_ALLOW_FILES = (
    "linux-6.8-main/kernel/sched/yat_casched.c",
    "linux-6.8-main/include/linux/sched/yat_casched.h",
    "linux-6.8-main/include/trace/events/yat_casched.h",
    "linux-6.8-main/yat_casched优化记录.md",
)

BINARY_EXT = {
    ".pdf", ".pptx", ".docx", ".jar", ".png", ".jpg", ".jpeg", ".gif",
    ".ico", ".zip", ".gz", ".xz", ".bz2", ".o", ".a", ".so", ".bin",
}

# 行级白名单：出现这些子串的行豁免
LINE_ALLOW_SUBSTRINGS = (
    "github.com/",          # URL 里的是仓库名，不可改
    "Yat-CAShed.tex",       # 已存在的论文主文件名
    "Yat-CAShed.pdf",       # 已存在的产物名
    "yat_CAshed.pdf",       # 历史产物名
    "Yat-CAShed_new.tex",   # 已删除文件的记录（CHANGELOG 历史条目）
    "name-check:ignore",    # 显式行级豁免（用于必须列举错误写法的规则说明）
)


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "-z"], capture_output=True, check=True
    ).stdout
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def in_scope(path: str) -> bool:
    if any(path.startswith(d) for d in EXEMPT_DIRS):
        return False
    if Path(path).suffix.lower() in BINARY_EXT:
        return False
    if path.startswith("linux-6.8-main/"):
        if path in KERNEL_ALLOW_FILES:
            return True
        return any(path.startswith(p) for p in KERNEL_ALLOW_PREFIXES)
    return True


def main() -> int:
    violations: list[tuple[str, int, str, str]] = []
    scanned = 0

    for rel in tracked_files():
        if not in_scope(rel):
            continue
        p = Path(rel)
        if not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        scanned += 1
        for lineno, line in enumerate(text.splitlines(), 1):
            if not VARIANT_RE.search(line):
                continue
            if any(s in line for s in LINE_ALLOW_SUBSTRINGS):
                continue
            for m in VARIANT_RE.finditer(line):
                violations.append((rel, lineno, m.group(0), line.strip()))

    print(f"name-check: 扫描 {scanned} 个文件，规范名 = {CANONICAL}")
    if not violations:
        print("✅ 项目名拼写一致（散文层无残留变体）")
        return 0

    by_variant: dict[str, int] = {}
    for _, _, v, _ in violations:
        by_variant[v] = by_variant.get(v, 0) + 1

    print(f"❌ 发现 {len(violations)} 处不一致：")
    for v, n in sorted(by_variant.items(), key=lambda kv: -kv[1]):
        print(f"   {n:4d}  {v}")
    print()
    for rel, lineno, v, ctx in violations:
        print(f"  {rel}:{lineno}: {v}  ->  {ctx[:140]}")
    print()
    print(f"修法：把上表中的写法改为 `{CANONICAL}`。")
    print("以下情形可豁免（请勿为此修改门禁，而是确认是否真的该改）：")
    print("  * 指向已存在文件/URL 的字面量（如 Yat-CAShed.tex）—— 文件名不是散文；")
    print("  * docs/archive/ 下的历史文档正文 —— 归档件保持原样。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
