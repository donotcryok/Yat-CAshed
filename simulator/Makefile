# ============================================================
# Cache-Aware vs WFD 算法对比实验 —— 跨平台 GNU Makefile
# ------------------------------------------------------------
# 在 Linux / macOS / Windows(MSYS2・Git Bash・GNU make) 下均可使用。
# Windows 原生 cmd 环境请改用 build.bat（行为等价）。
#
# 常用命令:
#   make compile      - 编译所有 Java 模块
#   make lib-test     - 运行第三方库依赖自检
#   make run          - 编译 + 主实验 + 可视化（等价旧版 make run）
#   make run-quick    - 快速小规模实验（用于冒烟/CI）
#   make verify       - 编译 + 库自检 + 快速实验（CI 友好）
#   make picture      - 仅生成可视化图表
#   make visualize    - 安装 Python 依赖并可视化
#   make clean        - 清理编译与结果产物
#   make all          - clean + compile + lib-test
# ============================================================

# ---------- 项目配置 ----------
JAVA_SRC     := .
CLASS_DIR    := classes
LIB_DIR      := lib
RESULT_DIR   := result
MAIN_CLASS   := AlgorithmComparisonExperiment
LIB_TEST_CLASS := LibraryVerification
TEST_DIR     := tests
TEST_CLASS   := tests.YatSimulatorTest

# ---------- 平台检测 ----------
ifeq ($(OS),Windows_NT)
  SEP := ;
  MKDIR_P = cmd /c if not exist "$(1)" mkdir "$(1)"
  RM_RF = cmd /c rmdir /s /q "$(1)"
else
  SEP := :
  MKDIR_P = mkdir -p "$(1)"
  RM_RF = rm -rf "$(1)"
endif

# ---------- 工具链（支持 JAVA_HOME 覆盖） ----------
ifdef JAVA_HOME
  JC := $(JAVA_HOME)/bin/javac
  JR := $(JAVA_HOME)/bin/java
else
  JC := javac
  JR := java
endif

CP := "$(LIB_DIR)/*$(SEP)$(CLASS_DIR)$(SEP)$(JAVA_SRC)"
JFLAGS := -encoding UTF-8

# 需要编译的模块目录（顺序即编译顺序，主/测试类随后）
MODULES := parameters entity allocation generator analyzer visualizer

.DEFAULT_GOAL := help
.PHONY: help check-java dirs compile lib-test test run run-quick verify picture picture-internal visualize clean all

# ---------- 帮助 ----------
help:
	@echo "=========================================="
	@echo " Cache-Aware vs WFD 算法对比实验构建系统"
	@echo "=========================================="
	@echo "  make compile      编译所有 Java 模块"
	@echo "  make lib-test     运行库依赖测试"
	@echo "  make test         运行单元测试（确定性核心逻辑）"
	@echo "  make run          完整流程（编译+实验+可视化）"
	@echo "  make run-quick    快速小规模实验（冒烟测试）"
	@echo "  make verify       编译 + 库自检 + 单元测试 + 快速实验（CI）"
	@echo "  make picture      仅生成可视化图表"
	@echo "  make visualize    安装 Python 依赖并可视化"
	@echo "  make clean        清理编译与结果产物"
	@echo "  make all          clean + compile + lib-test"
	@echo "=========================================="

# ---------- 前置检查 ----------
check-java:
	@command -v $(JC) >/dev/null 2>&1 || { \
	  echo "❌ 未找到 Java 编译器 '$(JC)'。请安装 JDK 17+ 或设置 JAVA_HOME。"; exit 1; }
	@$(JC) -version 2>&1 | sed 's/^/   javac: /'

dirs:
	@$(call MKDIR_P,$(CLASS_DIR))
	@$(call MKDIR_P,$(RESULT_DIR))

# ---------- 编译 ----------
compile: check-java dirs
	@echo "=========================================="
	@echo "开始编译所有 Java 模块..."
	@echo "=========================================="
	@for m in $(MODULES); do \
	  echo "  -> 编译 $$m ..."; \
	  $(JC) $(JFLAGS) -cp $(CP) -d $(CLASS_DIR) $$m/*.java || exit 1; \
	done
	@$(JC) $(JFLAGS) -cp $(CP) -d $(CLASS_DIR) $(LIB_TEST_CLASS).java
	@$(JC) $(JFLAGS) -cp $(CP) -d $(CLASS_DIR) $(MAIN_CLASS).java
	@echo "  -> 编译 $(TEST_DIR) ..."
	@$(JC) $(JFLAGS) -cp $(CP) -d $(CLASS_DIR) $(TEST_DIR)/*.java
	@echo "✅ 所有模块编译成功（产物: $(CLASS_DIR)/）"

# ---------- 库依赖自检 ----------
lib-test: compile
	@echo "=========================================="
	@echo "运行库依赖测试..."
	@echo "=========================================="
	@$(JR) -cp $(CP) $(LIB_TEST_CLASS)

# ---------- 单元测试（确定性核心逻辑） ----------
test: compile
	@$(JR) -cp $(CP) $(TEST_CLASS)

# ---------- 主实验（完整规模） ----------
run: compile
	@echo "=========================================="
	@echo "运行完整流程：WFD vs CacheAware_v2 对比实验"
	@echo "⏱️  预计 2-5 分钟"
	@echo "=========================================="
	@$(JR) -cp $(CP) $(MAIN_CLASS)
	@echo "📁 结果文件: $(RESULT_DIR)/algorithm_comparison_results.csv"
	@$(MAKE) --no-print-directory picture-internal || true

# ---------- 快速实验（小规模，用于冒烟/CI） ----------
QUICK_ARGS := --cases=5 --tasks=20 --seed=42 --no-viz
run-quick: compile
	@echo "=========================================="
	@echo "运行快速冒烟实验（5 案例 × 20 任务）..."
	@echo "=========================================="
	@$(JR) -cp $(CP) $(MAIN_CLASS) $(QUICK_ARGS)

# ---------- CI 校验：编译 + 库自检 + 单元测试 + 快速实验 ----------
verify: lib-test test run-quick
	@echo "=========================================="
	@echo "✅ verify 通过：可编译、依赖可用、单测通过、可运行"
	@echo "=========================================="

# ---------- 可视化 ----------
picture:
	@echo "=========================================="
	@echo "生成 Python 可视化结果图..."
	@echo "=========================================="
	@$(MAKE) --no-print-directory picture-internal

picture-internal:
	@if [ ! -f "$(RESULT_DIR)/algorithm_comparison_results.csv" ] && \
	   [ ! -f "$(RESULT_DIR)/experiment_results.csv" ] && \
	   [ ! -f "algorithm_comparison_results.csv" ] && \
	   [ ! -f "experiment_results.csv" ]; then \
	  echo "❌ 未找到实验结果文件，请先运行: make run"; exit 1; \
	fi
	@echo "📊 安装/检查 Python 依赖..."
	@cd visualizer && (pip install -q -r requirements.txt 2>/dev/null || pip3 install -q -r requirements.txt 2>/dev/null || true)
	@echo "🎨 生成图表..."
	@cd visualizer && (python visualize.py || python3 visualize.py)
	@echo "✅ 图表生成完成（输出目录: $(RESULT_DIR)/）"

visualize:
	@cd visualizer && pip install -r requirements.txt
	@cd visualizer && (python visualize.py || python3 visualize.py)

# ---------- 清理 ----------
clean:
	@echo "清理编译与结果产物..."
	@$(call RM_RF,$(CLASS_DIR)) 2>/dev/null || true
	@rm -f experiment_results.csv 2>/dev/null || true
	@rm -f $(RESULT_DIR)/*.png 2>/dev/null || true
	@rm -f *.png 2>/dev/null || true
	@echo "✓ 清理完成"

# ---------- 完整构建 ----------
all: clean compile lib-test test
	@echo "=========================================="
	@echo "✅ 完整构建与自检完成"
	@echo "   下一步: make run / make run-quick"
	@echo "=========================================="
