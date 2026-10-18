# Yat-CAShed 顶层编排 Makefile
# ------------------------------------------------------------
# 本文件仅作为各子模块既有构建入口的「薄封装」，不重复实现逻辑。
#   - 内核编译请在 linux-6.8-main/ 内执行（重量级，未纳入此处）
#   - 模拟器、可视化、论文均有各自的脚本/Makefile
# ------------------------------------------------------------

SHELL := /bin/bash
SIM_DIR := simulator
REPORT_DIR := report/paper

.DEFAULT_GOAL := help

.PHONY: help check docs-check name-check verify-all simulator simulator-compile simulator-quick simulator-verify simulator-test symbol-check visualize report clean tree homepage patch-check patch-apply patch-reverse patch-status patch-selftest kernel-setup kernel-obj kernel-build kernel-bench kernel-compare

help:
	@echo "=========================================="
	@echo " Yat-CAShed 顶层命令"
	@echo "=========================================="
	@echo "  make check             仓库健康检查（冲突标记/空文件/结构）"
	@echo "  make docs-check        Markdown 相对链接检查（失效链接即失败）"
	@echo "  make name-check        项目名拼写一致性检查（规范名 Yat-CAShed）"
	@echo "  make symbol-check      内核头文件/实现符号一致性检查（静态）"
	@echo "  make verify-all        check + docs-check + name-check + symbol-check + 补丁自检 + 模拟器全套验证"
	@echo ""
	@echo "  make patch-check       校验内核补丁包完整性（SHA256 + hunk 数）"
	@echo "  make patch-selftest    补丁 apply→比对→reverse→比对 往返测试"
	@echo "  make patch-apply       把补丁应用到 linux-6.8-main/"
	@echo "  make patch-reverse     从 linux-6.8-main/ 回退补丁"
	@echo "  make patch-status      报告每个补丁的应用状态"
	@echo "  make kernel-setup      从上游 v6.8 重建内核树（下载 + 打补丁）"
	@echo ""
	@echo "  make simulator-compile 仅编译 Java 模拟器"
	@echo "  make simulator-test    运行模拟器单元测试（确定性核心逻辑）"
	@echo "  make simulator         编译并运行完整模拟器实验（转发 simulator/Makefile）"
	@echo "  make simulator-quick   快速小规模模拟器实验（冒烟）"
	@echo "  make simulator-verify  模拟器编译+依赖+单测+快速实验（CI 等价）"
	@echo "  make visualize         生成模拟器可视化图表"
	@echo "  make report            编译 LaTeX 论文（需 xelatex）"
	@echo "  make tree              打印项目结构"
	@echo "  make homepage          本地预览项目主页（http://localhost:8000）"
	@echo "  make clean             清理各模块构建产物"
	@echo ""
	@echo "  ---- 真实内核 ----"
	@echo "  make kernel-obj        只编译调度器目标文件（最快的语法/类型检查）"
	@echo "  make kernel-build      全量编译内核（产出 bzImage）"
	@echo "  make kernel-bench      仅构建 CFS vs Yat 基准程序（不启动 QEMU）"
	@echo "  make kernel-compare    QEMU 内跑同一负载，CFS vs Yat 自动出 CSV"
	@echo ""
	@echo "  测试环境： cd linux-6.8-main/yat_test_scripts && ./start_test_environment.sh"
	@echo "=========================================="

# 仓库健康检查：发现合并冲突标记或空源文件即失败
check:
	@bash scripts/check_repo.sh

# 文档健康检查：Markdown 相对链接必须指向真实文件
docs-check:
	@python3 scripts/check_docs.py

# 项目名拼写一致性：规范名 Yat-CAShed；豁免 docs/archive（历史快照）、
# report（论文交付物）、patches（生成物）与二进制。
name-check:
	@python3 scripts/check_names.py

# 内核头文件声明 vs 实现 符号一致性检查（静态，不修改源码）
symbol-check:
	@bash scripts/check_symbols.sh

# 一键全量门禁（CI 等价的最小集合）
verify-all: check docs-check name-check symbol-check patch-check simulator-verify
	@echo ">>> 全部门禁通过"

# ---- 内核补丁包（替代 1.6GB 内核整树入库）----
PATCH_DIR := patches

patch-check:
	@bash scripts/kernel-patch.sh --check

patch-selftest:
	@bash scripts/kernel-patch.sh --selftest --upstream "$${UPSTREAM:-/tmp/up}"

patch-apply:
	@bash scripts/kernel-patch.sh --apply

patch-reverse:
	@bash scripts/kernel-patch.sh --reverse

patch-status:
	@bash scripts/kernel-patch.sh --status

kernel-setup:
	@bash scripts/setup-kernel.sh

# ---- 真实内核（linux-6.8-main/）----
# 目标文件级别的编译是最便宜的正确性闸门：它能在秒级暴露头文件、
# 类型、宏展开问题，而不必等全量链接。
KERNEL_DIR := linux-6.8-main

kernel-obj:
	@$(MAKE) -C $(KERNEL_DIR) kernel/sched/yat_casched.o

kernel-build:
	@$(MAKE) -C $(KERNEL_DIR) -j"$$(nproc)"

# 只编译基准程序，用于快速确认用户态代码没被改坏（无需内核）。
kernel-bench:
	@gcc -O2 -static -Wall -Wextra \
		-o /tmp/yat_bench $(KERNEL_DIR)/boot_test_scripts/yat_bench.c
	@echo ">>> /tmp/yat_bench 构建完成"

# 同一负载、同一 CPU，只改调度策略；两次 QEMU 启动各自从干净状态开始。
kernel-compare:
	@cd $(KERNEL_DIR)/yat_test_scripts && ./compare_cfs_vs_yat.sh

# 模拟器（simulator/Makefile 现已跨平台）
simulator:
	@$(MAKE) -C $(SIM_DIR) run

simulator-compile:
	@$(MAKE) -C $(SIM_DIR) compile

simulator-test:
	@$(MAKE) -C $(SIM_DIR) test

simulator-quick:
	@$(MAKE) -C $(SIM_DIR) run-quick

simulator-verify:
	@$(MAKE) -C $(SIM_DIR) verify

visualize:
	@$(MAKE) -C $(SIM_DIR) picture

# 论文编译：优先 latexmk（自动处理多次 xelatex），缺失时回退到手工两遍
report:
	@cd $(REPORT_DIR) && \
		if command -v latexmk >/dev/null 2>&1; then \
			latexmk -xelatex -interaction=nonstopmode -halt-on-error Yat-CAShed.tex; \
		else \
			xelatex -interaction=nonstopmode -halt-on-error Yat-CAShed.tex && \
			xelatex -interaction=nonstopmode -halt-on-error Yat-CAShed.tex; \
		fi

tree:
	@find . -maxdepth 2 -not -path './.git*' -not -path './linux-6.8-main/*' | sort

# 本地预览项目主页：docs/ 下有 index.html + .nojekyll，直接起静态服务即可。
# GitHub Pages 发布时把 Source 设为 main /docs，无需任何构建步骤。
homepage:
	@echo ">>> http://localhost:8000（Ctrl+C 结束）"
	@python3 -m http.server 8000 --directory docs

clean:
	@echo ">>> 清理模拟器产物..."
	@rm -rf $(SIM_DIR)/classes $(SIM_DIR)/out $(SIM_DIR)/result/*.png 2>/dev/null || true
	@echo ">>> 清理 LaTeX 辅助文件..."
	@find $(REPORT_DIR) -maxdepth 1 \( -name '*.aux' -o -name '*.log' -o -name '*.toc' \
		-o -name '*.out' -o -name '*.synctex.gz' -o -name '*.fls' -o -name '*.fdb_latexmk' \) -delete 2>/dev/null || true
	@echo ">>> 完成（内核构建产物请在内核目录用 make clean/mrproper 清理）"
