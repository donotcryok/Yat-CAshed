# Cache-Aware vs WFD 算法对比实验 Makefile
# 
# 使用方法:
#   make compile    - 编译所有模块
#   make lib-test   - 运行库依赖测试
#   make verify     - 运行快速验证测试
#   make run        - 运行主实验程序
#   make clean      - 清理编译文件
#   make all        - 完整构建和测试流程

# 项目配置
JAVA_SRC = .
CLASS_DIR = classes
LIB_DIR = lib
MAIN_CLASS = AlgorithmComparisonExperiment
LIB_TEST_CLASS = LibraryVerification

# Java编译器配置
JAVAC = javac
JAVA = java
CLASSPATH = "$(LIB_DIR)/*;$(CLASS_DIR);$(JAVA_SRC)"

# 默认目标
.DEFAULT_GOAL := help

# 显示帮助信息
help:
	@echo "=========================================="
	@echo "Cache-Aware vs WFD 算法对比实验构建系统"
	@echo "=========================================="
	@echo "可用命令:"
	@echo "  make compile    - 编译所有Java模块"
	@echo "  make lib-test   - 运行库依赖测试"
	@echo "  make run        - 运行完整流程（编译+实验+可视化）"
	@echo "  make picture    - 仅生成Python可视化结果图"
	@echo "  make clean      - 清理编译文件"
	@echo "  make all        - 完整构建和测试流程"
	@echo "  make visualize  - 安装Python依赖并运行可视化"
	@echo ""
	@echo "推荐使用: make run （一键完成所有任务）"
	@echo "=========================================="

# 创建必要的目录
$(CLASS_DIR):
	@if not exist "$(CLASS_DIR)" mkdir "$(CLASS_DIR)"

# 编译所有模块
compile: $(CLASS_DIR)
	@echo "=========================================="
	@echo "开始编译所有Java模块..."
	@echo "=========================================="
	@echo "1. 编译parameters模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) parameters/*.java
	@echo "✓ parameters模块编译完成"
	
	@echo "2. 编译entity模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) entity/*.java
	@echo "✓ entity模块编译完成"
	
	@echo "3. 编译allocation模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) allocation/*.java
	@echo "✓ allocation模块编译完成"
	
	@echo "4. 编译generator模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) generator/*.java
	@echo "✓ generator模块编译完成"
	
	@echo "5. 编译analyzer模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) analyzer/*.java
	@echo "✓ analyzer模块编译完成"
		@echo "6. 编译visualizer模块..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) visualizer/*.java
	@echo "✓ visualizer模块编译完成"
	
	@echo "7. 编译主程序和测试程序..."
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) $(LIB_TEST_CLASS).java
	$(JAVAC) -cp $(CLASSPATH) -d $(CLASS_DIR) $(MAIN_CLASS).java
	@echo "✓ 主程序和测试程序编译完成"
	
	@echo "=========================================="
	@echo "🎉 所有模块编译成功！"
	@echo "=========================================="

# 运行库依赖测试
lib-test: compile
	@echo "=========================================="
	@echo "运行库依赖测试..."
	@echo "=========================================="
	$(JAVA) -cp $(CLASSPATH) $(LIB_TEST_CLASS)
	@echo "=========================================="
	@echo "✓ 库依赖测试完成"
	@echo "=========================================="

# 运行主实验程序
run: compile
	@echo "=========================================="
	@echo "运行完整流程：WFD vs CacheAware_v2算法对比实验..."
	@echo "=========================================="
	@echo "🎯 使用重命名后的算法进行对比"
	@echo "📊 WFD: 经典负载均衡算法"
	@echo "⚡ CacheAware_v2: 增强版缓存感知算法"
	@echo "⏱️  预计运行时间: 2-5分钟（取决于系统性能）"
	@echo "=========================================="
	$(JAVA) -cp $(CLASSPATH) $(MAIN_CLASS)
	@echo "=========================================="
	@echo "🎉 算法对比实验运行完成！"
	@echo "📁 结果文件: algorithm_comparison_results.csv"
	@echo "=========================================="
	@echo ""
	@echo "🎨 自动生成可视化图表..."
	@$(MAKE) picture-internal
	@echo "=========================================="
	@echo "✅ 完整流程执行成功！实验数据和图表都已生成"
	@echo "=========================================="

# 生成Python可视化结果图
picture:
	@echo "=========================================="
	@echo "生成Python可视化结果图 v2.0..."
	@echo "=========================================="
	@if exist "result\algorithm_comparison_results.csv" (echo "✓ 找到实验结果文件: result\algorithm_comparison_results.csv") else if exist "algorithm_comparison_results.csv" (echo "✓ 找到实验结果文件: algorithm_comparison_results.csv") else if exist "result\experiment_results.csv" (echo "✓ 找到实验结果文件: result\experiment_results.csv") else if exist "experiment_results.csv" (echo "✓ 找到实验结果文件: experiment_results.csv") else (echo "❌ 未找到实验结果文件，请先运行: make run" && echo "   查找路径: result\algorithm_comparison_results.csv 或 algorithm_comparison_results.csv" && exit /b 1)
	@echo "📊 检查Python环境和依赖..."
	@cd visualizer && pip install -q -r requirements.txt
	@echo "🎨 使用全新设计的可视化程序生成图表..."
	@cd visualizer && python visualize.py
	@echo "=========================================="
	@echo "✅ 可视化图表生成完成！"
	@echo "生成的文件位置: result\"
	@if exist "result\makespan_analysis.png" echo "  ✓ makespan_analysis.png - Makespan详细分析"
	@if exist "result\cache_hit_analysis.png" echo "  ✓ cache_hit_analysis.png - 缓存命中率详细分析"
	@if exist "result\execution_time_and_win_rate_analysis.png" echo "  ✓ execution_time_and_win_rate_analysis.png - 执行时间和胜率分析"	@echo "=========================================="

# 内部可视化目标（供run自动调用）
picture-internal:
	@if exist "result\algorithm_comparison_results.csv" (echo "✓ 找到实验结果文件: result\algorithm_comparison_results.csv") else if exist "algorithm_comparison_results.csv" (echo "✓ 找到实验结果文件: algorithm_comparison_results.csv") else if exist "result\experiment_results.csv" (echo "✓ 找到实验结果文件: result\experiment_results.csv") else if exist "experiment_results.csv" (echo "✓ 找到实验结果文件: experiment_results.csv") else (echo "❌ 未找到实验结果文件" && exit /b 1)
	@echo "📊 检查Python环境和依赖..."
	@cd visualizer && pip install -q -r requirements.txt
	@echo "🎨 使用全新设计的可视化程序生成图表..."
	@cd visualizer && python visualize.py
	@echo "✅ 可视化图表生成完成！"
	@echo "生成的文件位置: result\"
	@if exist "result\makespan_analysis.png" echo "  ✓ makespan_analysis.png - Makespan Analysis"
	@if exist "result\cache_hit_analysis.png" echo "  ✓ cache_hit_analysis.png - Cache Hit Rate Analysis"
	@if exist "result\execution_time_and_win_rate_analysis.png" echo "  ✓ execution_time_and_win_rate_analysis.png - Execution Time and Win Rate Analysis"

# 安装Python依赖并运行可视化
visualize:
	@echo "=========================================="
	@echo "安装Python可视化依赖..."
	@echo "=========================================="
	cd visualizer && pip install -r requirements.txt
	@echo "✓ Python依赖安装完成"
	@echo "运行全新设计的可视化程序 v2.0..."
	cd visualizer && python visualize.py
	@echo "✓ 可视化完成"

# 清理编译文件
clean:
	@echo "=========================================="
	@echo "清理编译文件..."
	@echo "=========================================="
	@if exist "$(CLASS_DIR)" rmdir /s /q "$(CLASS_DIR)"
	@if exist "experiment_results.csv" del "experiment_results.csv"
	@if exist "*.png" del "*.png"
	@echo "✓ 清理完成"

# 完整构建和测试流程
all: clean compile lib-test
	@echo "=========================================="
	@echo "🎉 完整构建和测试流程完成！"
	@echo "=========================================="
	@echo "现在可以运行以下命令："
	@echo "  make run        - 运行主实验"
	@echo "  make visualize  - 运行可视化"
	@echo "=========================================="

# 标记伪目标
.PHONY: help compile lib-test run picture picture-internal visualize clean all
