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

.PHONY: help check simulator simulator-compile simulator-quick simulator-verify simulator-test symbol-check visualize report clean tree

help:
	@echo "=========================================="
	@echo " Yat-CAShed 顶层命令"
	@echo "=========================================="
	@echo "  make check             仓库健康检查（冲突标记/空文件/结构）"
	@echo "  make symbol-check      内核头文件/实现符号一致性检查（静态）"
	@echo "  make simulator-compile 仅编译 Java 模拟器"
	@echo "  make simulator-test    运行模拟器单元测试（确定性核心逻辑）"
	@echo "  make simulator         编译并运行完整模拟器实验（转发 simulator/Makefile）"
	@echo "  make simulator-quick   快速小规模模拟器实验（冒烟）"
	@echo "  make simulator-verify  模拟器编译+依赖+单测+快速实验（CI 等价）"
	@echo "  make visualize         生成模拟器可视化图表"
	@echo "  make report            编译 LaTeX 论文（需 xelatex）"
	@echo "  make tree              打印项目结构"
	@echo "  make clean             清理各模块构建产物"
	@echo ""
	@echo "  内核编译： cd linux-6.8-main && make kernel/sched/yat_casched.o"
	@echo "  测试环境： cd linux-6.8-main/yat_test_scripts && ./start_test_environment.sh"
	@echo "=========================================="

# 仓库健康检查：发现合并冲突标记或空源文件即失败
check:
	@bash scripts/check_repo.sh

# 内核头文件声明 vs 实现 符号一致性检查（静态，不修改源码）
symbol-check:
	@bash scripts/check_symbols.sh

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

report:
	@cd $(REPORT_DIR) && xelatex -interaction=nonstopmode Yat-CAShed.tex && \
		xelatex -interaction=nonstopmode Yat-CAShed.tex

tree:
	@find . -maxdepth 2 -not -path './.git*' -not -path './linux-6.8-main/*' | sort

clean:
	@echo ">>> 清理模拟器产物..."
	@rm -rf $(SIM_DIR)/classes $(SIM_DIR)/out $(SIM_DIR)/result/*.png 2>/dev/null || true
	@echo ">>> 清理 LaTeX 辅助文件..."
	@find $(REPORT_DIR) -maxdepth 1 \( -name '*.aux' -o -name '*.log' -o -name '*.toc' \
		-o -name '*.out' -o -name '*.synctex.gz' -o -name '*.fls' -o -name '*.fdb_latexmk' \) -delete 2>/dev/null || true
	@echo ">>> 完成（内核构建产物请在内核目录用 make clean/mrproper 清理）"
