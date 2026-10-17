import java.io.File;
import java.text.DecimalFormat;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

import allocation.WFD;
import allocation.CacheAware_v2;
import analyzer.PerformanceAnalyzer;
import analyzer.PerformanceAnalyzer.ExperimentResult;
import entity.Node;
import generator.EnhancedTaskGenerator;
import parameters.SystemParameters;
import visualizer.ResultVisualizer;

/**
 * 算法性能对比实验主类
 * 
 * 该类实现了WFD算法与Cache-Aware算法的全面性能对比实验。
 * 通过大规模的测试案例和多维度的性能指标评估，验证Cache-Aware算法的性能优势。
 * 
 * 实验设计特点：
 * - 多利用率级别测试：覆盖不同系统负载情况
 * - 大规模测试案例：确保统计结果的可靠性
 * - 缓存敏感任务生成：突出缓存算法的优势
 * - 多维度性能评估：从makespan到缓存命中率的全面分析
 * - 实时进度监控：提供详细的实验进度和中间结果
 * - 自动化可视化：生成图表和CSV数据用于进一步分析
 * 
 * 核心实验参数：
 * - 处理器核心数：8核
 * - 测试案例数：100个
 * - 每案例任务数：60个
 * - 利用率级别：0.6, 0.8, 1.0, 1.2, 1.5
 * - 高缓存敏感任务比例：70%
 * 
 * 评估指标：
 * 1. Makespan（总完成时间）
 * 2. 缓存命中率
 * 3. CPU利用率
 * 4. 负载均衡度
 * 5. 算法执行时间
 * 6. 胜率统计
 * 
 * @author Cache-Aware Task Scheduling System
 */
public class AlgorithmComparisonExperiment {    
    // ==================== 实验配置常量 ====================
    
    /** 系统处理器核心数量（可通过 --cores= 覆盖） */
    private static int CORES = 8;
    
    /** 总测试案例数量（可通过 --cases= 覆盖） */
    private static int TOTAL_TEST_CASES = 100;
    
    /** 每个测试案例的任务数量（可通过 --tasks= 覆盖） */
    private static int TASKS_PER_CASE = 60;

    /** 随机种子（可通过 --seed= 覆盖，保证可复现） */
    private static long SEED = 42;

    /** 是否生成可视化（--no-viz 关闭；快速/CI 场景建议关闭） */
    private static boolean ENABLE_VIZ = true;
    
    /** 
     * 系统利用率测试级别数组
     * 覆盖从轻载到重载的不同系统负载情况
     */
    private static final double[] UTILIZATION_LEVELS = {
        0.6,  // 轻载：60%利用率
        0.8,  // 中载：80%利用率  
        1.0,  // 满载：100%利用率
        1.2,  // 轻度过载：120%利用率
        1.5   // 重度过载：150%利用率
    };
    
    /** 
     * 高缓存敏感任务比例
     * 70%的任务具有高缓存敏感度，用于突出Cache-Aware算法的优势
     */
    private static final double HIGH_CACHE_SENSITIVITY_RATIO = 0.7;
    
    /** 性能分析器实例 */
    private static PerformanceAnalyzer analyzer;
    
    /** 结果可视化器实例 */
    private static ResultVisualizer visualizer;
      /**
     * 主程序入口
     * 
     * 启动WFD vs Cache-Aware算法的全面性能对比实验。
     * 包括系统参数设置、实验执行、结果分析和可视化生成。
     * 
     * @param args 命令行参数（当前未使用）
     */
    /**
     * 解析命令行参数，使实验规模/种子可配置（便于 CI 快速冒烟与可复现实验）。
     *
     * 支持:
     *   --cores=N      处理器核心数（默认 8）
     *   --cases=N      总测试案例数（默认 100）
     *   --tasks=N      每案例任务数（默认 60）
     *   --seed=N       随机种子（默认 42）
     *   --no-viz       跳过 Python 可视化（快速/CI 场景）
     *   -h, --help     打印帮助
     */
    private static void parseArgs(String[] args) {
        for (String arg : args) {
            try {
                if (arg.equals("--no-viz")) {
                    ENABLE_VIZ = false;
                } else if (arg.equals("-h") || arg.equals("--help")) {
                    System.out.println("用法: java AlgorithmComparisonExperiment "
                        + "[--cores=N] [--cases=N] [--tasks=N] [--seed=N] [--no-viz]");
                    System.exit(0);
                } else if (arg.startsWith("--cores=")) {
                    CORES = Math.max(1, Integer.parseInt(arg.substring(8)));
                } else if (arg.startsWith("--cases=")) {
                    TOTAL_TEST_CASES = Math.max(1, Integer.parseInt(arg.substring(8)));
                } else if (arg.startsWith("--tasks=")) {
                    TASKS_PER_CASE = Math.max(1, Integer.parseInt(arg.substring(8)));
                } else if (arg.startsWith("--seed=")) {
                    SEED = Long.parseLong(arg.substring(7));
                } else {
                    System.out.println("⚠️  忽略未知参数: " + arg);
                }
            } catch (NumberFormatException e) {
                System.err.println("❌ 参数格式错误: " + arg + "（应为 --key=整数）");
                System.exit(2);
            }
        }
    }

    public static void main(String[] args) {
        parseArgs(args);
        System.out.println("=== WFD vs CacheAware_v2算法性能对比实验 ===");
        System.out.println("🎯 目标：验证CacheAware_v2算法的性能优势");
        System.out.println("📊 实验规模: " + TOTAL_TEST_CASES + " 个测试案例");
        System.out.println("💻 处理器核心数: " + CORES);
        System.out.println("📋 每案例任务数: " + TASKS_PER_CASE);
        System.out.println("⚡ 高缓存敏感任务比例: " + (HIGH_CACHE_SENSITIVITY_RATIO * 100) + "%");
        System.out.println("📈 利用率级别: " + java.util.Arrays.toString(UTILIZATION_LEVELS));
        System.out.println();
        
        // 初始化分析和可视化组件
        analyzer = new PerformanceAnalyzer();
        visualizer = new ResultVisualizer();
        
        // 设置系统参数
        setupSystemParameters();
        
        // 运行对比实验
        runComparison();
        
        System.out.println("=== 实验完成 ===");
    }
      /**
     * 设置系统参数配置
     * 
     * 为Cache-Aware算法优化系统参数，重点配置缓存层次结构和权重分配。
     * 这些参数直接影响算法的性能表现和缓存命中率计算。
     */
    private static void setupSystemParameters() {
        // 设置处理器核心数
        SystemParameters.coreNum = CORES;
        
        // 配置缓存层次权重 - 更重视L1缓存的影响
        SystemParameters.cc_weights = new double[]{0.5, 0.3, 0.15, 0.05}; 
        SystemParameters.cacheLevel = 3; // 三级缓存架构
        SystemParameters.Level2CoreNum = 2; // L2缓存由2个核心共享
        
        System.out.println("🔧 系统参数配置:");
        System.out.println("   缓存权重: L1=" + SystemParameters.cc_weights[0] + 
                          " L2=" + SystemParameters.cc_weights[1] + 
                          " L3=" + SystemParameters.cc_weights[2] + 
                          " Mem=" + SystemParameters.cc_weights[3]);
        System.out.println("   L2缓存共享: 每" + SystemParameters.Level2CoreNum + "个核心共享");
        System.out.println();
    }
      /**
     * 运行算法对比实验主流程
     * 
     * 该方法是整个实验的核心，负责：
     * 1. 循环执行多个利用率级别的测试
     * 2. 为每个测试案例生成缓存敏感的任务集
     * 3. 分别执行WFD和Cache-Aware算法
     * 4. 收集和分析性能数据
     * 5. 生成实时进度报告
     * 6. 统计胜负情况和性能改进
     * 7. 生成最终分析报告和可视化
     */
    private static void runComparison() {
        System.out.println("🚀 开始WFD vs CacheAware_v2对比实验...\n");
        
        // 初始化随机数生成器和任务生成器
        Random rng = new Random(SEED); // 固定种子确保结果可重复（可由 --seed= 覆盖）
        EnhancedTaskGenerator taskGenerator = new EnhancedTaskGenerator(CORES, rng);
        
        // 存储各算法的实验结果
        Map<String, List<ExperimentResult>> algorithmResults = new HashMap<>();
        algorithmResults.put("WFD", new ArrayList<>());
        algorithmResults.put("CacheAware_v2", new ArrayList<>());
        
        // 性能统计变量
        int completedTests = 0;
        int wfdWins = 0;
        int cacheAwareWins = 0;
        double totalMakespanImprovement = 0.0;
        double totalCacheHitImprovement = 0.0;
        
        // 对每个利用率级别进行测试
        // 将总案例数尽量均匀分配到各利用率级别（修正整数除法导致案例被丢弃的问题）
        int levels = UTILIZATION_LEVELS.length;
        int baseTestsPerLevel = TOTAL_TEST_CASES / levels;
        int remainder = TOTAL_TEST_CASES % levels;
        for (int levelIdx = 0; levelIdx < levels; levelIdx++) {
            double utilization = UTILIZATION_LEVELS[levelIdx];
            // 前 remainder 个级别各多分 1 个案例，保证总数精确等于 TOTAL_TEST_CASES
            int testsPerLevel = baseTestsPerLevel + (levelIdx < remainder ? 1 : 0);
            testsPerLevel = Math.max(1, testsPerLevel);
            
            System.out.println("📊 测试利用率级别: " + utilization);
            
            for (int test = 0; test < testsPerLevel; test++) {
                completedTests++;
                
                // 生成缓存敏感的任务集
                List<Node> originalTasks = taskGenerator.generateCacheIntensiveTasks(
                    TASKS_PER_CASE, utilization, HIGH_CACHE_SENSITIVITY_RATIO
                );
                
                // === WFD算法测试 ===
                List<Node> wfdTasks = cloneTasks(originalTasks);
                List<Integer> wfdProcessors = generateProcessorList(CORES);
                WFD wfdAlgorithm = new WFD();
                
                long wfdStartTime = System.nanoTime();
                simulateWFDAllocation(wfdTasks, wfdProcessors, wfdAlgorithm);
                long wfdEndTime = System.nanoTime();
                  ExperimentResult wfdResult = analyzer.analyzeAlgorithmPerformance(
                    "WFD", completedTests, wfdTasks, CORES, 
                    (wfdEndTime - wfdStartTime) / 1_000_000.0, utilization
                );
                algorithmResults.get("WFD").add(wfdResult);
                
                // === CacheAware_v2算法测试 ===
                List<Node> cacheTasks = cloneTasks(originalTasks);
                List<Integer> cacheProcessors = generateProcessorList(CORES);
                CacheAware_v2 cacheAlgorithm = new CacheAware_v2();
                
                // 重置缓存状态
                CacheAware_v2.resetState();
                
                long cacheStartTime = System.nanoTime();
                simulateCacheAwareAllocation(cacheTasks, cacheProcessors, cacheAlgorithm);
                long cacheEndTime = System.nanoTime();
                
                ExperimentResult cacheResult = analyzer.analyzeAlgorithmPerformance(
                    "CacheAware_v2", completedTests, cacheTasks, CORES,
                    (cacheEndTime - cacheStartTime) / 1_000_000.0, utilization
                );
                algorithmResults.get("CacheAware_v2").add(cacheResult);
                
                // 计算性能差异
                double makespanImprovement = ((double)(wfdResult.makespan - cacheResult.makespan)) / wfdResult.makespan;
                double cacheHitImprovement = cacheResult.cacheHitRatio - wfdResult.cacheHitRatio;
                
                totalMakespanImprovement += makespanImprovement;
                totalCacheHitImprovement += cacheHitImprovement;
                
                // 胜负统计
                if (cacheResult.makespan < wfdResult.makespan) {
                    cacheAwareWins++;
                } else {
                    wfdWins++;
                }
                
                // 详细进度报告（每10个案例）
                if (completedTests % 10 == 0) {
                    System.out.printf("✅ 完成 %d/%d 测试案例\n", completedTests, TOTAL_TEST_CASES);
                    System.out.printf("   当前makespan改进: %.2f%%\n", makespanImprovement * 100);
                    System.out.printf("   当前缓存命中率改进: %.3f\n", cacheHitImprovement);
                    System.out.printf("   CacheAware_v2胜率: %.1f%%\n", 
                                     (double)cacheAwareWins / completedTests * 100);
                    
                    // 显示缓存状态报告
                    System.out.println("   🗄️ 缓存状态快照:");
                    Map<Integer, String> cacheReport = CacheAware_v2.getCacheStateReport();
                    for (Map.Entry<Integer, String> entry : cacheReport.entrySet()) {
                        System.out.println("     " + entry.getValue());
                    }
                    System.out.println();
                }
            }
        }
        
        // 生成详细的分析报告
        generateReport(algorithmResults, cacheAwareWins, wfdWins, completedTests,
                      totalMakespanImprovement, totalCacheHitImprovement);
        
        // 生成可视化图表（快速/CI 场景可用 --no-viz 跳过）
        if (ENABLE_VIZ) {
            generateVisualization(algorithmResults);
        } else {
            System.out.println("ℹ️  已通过 --no-viz 跳过可视化生成");
        }
        
        // 重置算法状态
        WFD.resetLoads();
        CacheAware_v2.resetState();
    }    /**
     * 模拟Cache-Aware算法的任务分配过程
     * 
     * 该方法模拟Cache-Aware算法的完整执行过程，包括：
     * 1. 任务分配决策
     * 2. 缓存感知的执行时间计算
     * 3. 处理器负载更新
     * 4. 任务调度时间计算
     * 
     * @param tasks 要分配的任务列表
     * @param processors 可用处理器列表
     * @param algorithm Cache-Aware算法实例
     */
    private static void simulateCacheAwareAllocation(List<Node> tasks, 
            List<Integer> processors, CacheAware_v2 algorithm) {
        
        // 处理器负载跟踪数组
        long[] processorLoads = new long[processors.size()];
        
        // 逐个处理任务
        for (Node task : tasks) {
            // 准备就绪任务列表（Cache-Aware算法每次处理一个任务）
            List<Node> readyTasks = new ArrayList<>();
            readyTasks.add(task);
            
            // 调用Cache-Aware算法进行任务分配
            int allocatedProcessor = algorithm.allocate(readyTasks, processors);
            
            if (allocatedProcessor != -1) {
                // 记录任务分配结果
                task.partition = allocatedProcessor;
                
                // 使用Cache-Aware算法的执行时间计算
                // 这里会考虑缓存优化带来的性能提升
                long enhancedET = CacheAware_v2.calculateExecutionTime(task, allocatedProcessor);
                
                // 更新任务的实际执行时间
                task.expectedET = enhancedET;
                
                // 计算任务的调度时间
                task.start = processorLoads[allocatedProcessor];
                task.finishAt = task.start + enhancedET;
                processorLoads[allocatedProcessor] = task.finishAt;
                task.finish = true;
            }
        }
    }    /**
     * 模拟WFD算法的任务分配过程
     * 
     * 该方法模拟传统WFD算法的执行过程，作为对比基准：
     * 1. 基于负载均衡的简单分配策略
     * 2. 使用原始执行时间（无缓存优化）
     * 3. 处理器负载更新
     * 4. 任务调度时间计算
     * 
     * @param tasks 要分配的任务列表
     * @param processors 可用处理器列表  
     * @param algorithm WFD算法实例
     */
    private static void simulateWFDAllocation(List<Node> tasks, List<Integer> processors, WFD algorithm) {
        // 处理器负载跟踪数组
        long[] processorLoads = new long[processors.size()];
        
        // 逐个处理任务
        for (Node task : tasks) {
            // 准备就绪任务列表（WFD算法每次处理一个任务）
            List<Node> readyTasks = new ArrayList<>();
            readyTasks.add(task);
            
            // 调用WFD算法进行任务分配
            int allocatedProcessor = algorithm.allocate(readyTasks, processors);
            
            if (allocatedProcessor != -1) {
                // 记录任务分配结果
                task.partition = allocatedProcessor;
                
                // WFD使用原始执行时间（无缓存优化）
                long executionTime = task.expectedET;
                
                // 计算任务的调度时间
                task.start = processorLoads[allocatedProcessor];
                task.finishAt = task.start + executionTime;
                processorLoads[allocatedProcessor] = task.finishAt;
                task.finish = true;
            }
        }
    }
      /**
     * 克隆任务列表（深拷贝）
     */    private static List<Node> cloneTasks(List<Node> original) {
        List<Node> cloned = new ArrayList<>();
        Random rng = new Random(42); // 用于创建Node的随机数生成器
        
        for (Node node : original) {
            // 使用Node的正确构造函数
            Node clonedNode = new Node(node.getLayer(), node.getType(), node.getId(), 
                                     node.getDagID(), node.crp, rng);
            
            // 复制关键属性
            clonedNode.expectedET = node.expectedET;
            clonedNode.sensitivity = node.sensitivity;
            clonedNode.affinity = node.affinity;
            
            if (node.weights != null) {
                clonedNode.weights = node.weights.clone();
            }
            
            cloned.add(clonedNode);
        }
        return cloned;
    }
    
    /**
     * 生成处理器列表
     */
    private static List<Integer> generateProcessorList(int cores) {
        List<Integer> processors = new ArrayList<>();
        for (int i = 0; i < cores; i++) {
            processors.add(i);
        }
        return processors;
    }
    
    /**
     * 生成详细的分析报告
     */
    private static void generateReport(Map<String, List<ExperimentResult>> algorithmResults, 
            int cacheAwareWins, int wfdWins, int totalTests,
            double totalMakespanImprovement, double totalCacheHitImprovement) {
        
        System.out.println("\n" + "=".repeat(80));
        System.out.println("📈 WFD vs CacheAware_v2实验结果详细分析报告");
        System.out.println("=".repeat(80));
        
        // 基础统计
        double avgMakespanImprovement = (totalMakespanImprovement / totalTests) * 100;
        double avgCacheHitImprovement = (totalCacheHitImprovement / totalTests) * 100;
        double winRate = (double)cacheAwareWins / totalTests * 100;
        
        System.out.println("🏆 总体性能对比:");
        System.out.printf("   📊 测试案例总数: %d\n", totalTests);
        System.out.printf("   ⏱️  平均Makespan改进: %.2f%%\n", avgMakespanImprovement);
        System.out.printf("   🎯 平均缓存命中率改进: %.2f%%\n", avgCacheHitImprovement);
        System.out.printf("   🏅 CacheAware_v2胜率: %d/%d = %.1f%%\n", 
                          cacheAwareWins, totalTests, winRate);
        
        // 详细统计分析
        List<ExperimentResult> wfdResults = algorithmResults.get("WFD");
        List<ExperimentResult> cacheResults = algorithmResults.get("CacheAware_v2");
          // 计算统计指标
        double wfdAvgMakespan = wfdResults.stream().mapToDouble(r -> r.makespan).average().orElse(0);
        double cacheAvgMakespan = cacheResults.stream().mapToDouble(r -> r.makespan).average().orElse(0);
        double wfdAvgCacheHit = wfdResults.stream().mapToDouble(r -> r.cacheHitRatio).average().orElse(0);
        double cacheAvgCacheHit = cacheResults.stream().mapToDouble(r -> r.cacheHitRatio).average().orElse(0);
        
        System.out.println("\n📋 详细性能指标:");
        System.out.printf("   WFD算法:\n");
        System.out.printf("     - 平均Makespan: %.0f ns\n", wfdAvgMakespan);
        System.out.printf("     - 平均缓存命中率: %.3f (%.1f%%)\n", wfdAvgCacheHit, wfdAvgCacheHit * 100);
        
        System.out.printf("   CacheAware_v2算法:\n");
        System.out.printf("     - 平均Makespan: %.0f ns\n", cacheAvgMakespan);
        System.out.printf("     - 平均缓存命中率: %.3f (%.1f%%)\n", cacheAvgCacheHit, cacheAvgCacheHit * 100);
        
        System.out.printf("   性能改进:\n");
        System.out.printf("     - Makespan减少: %.0f ns (%.2f%%)\n", 
                          wfdAvgMakespan - cacheAvgMakespan, 
                          ((wfdAvgMakespan - cacheAvgMakespan) / wfdAvgMakespan) * 100);
        System.out.printf("     - 缓存命中率提升: %.3f (%.1f个百分点)\n", 
                          cacheAvgCacheHit - wfdAvgCacheHit,
                          (cacheAvgCacheHit - wfdAvgCacheHit) * 100);
        
        // 结果评估
        System.out.println("\n🔍 结果评估:");
        if (avgMakespanImprovement > 15) {
            System.out.println("   ✅ Makespan改进显著 (>15%)，CacheAware_v2算法优势明显");
        } else if (avgMakespanImprovement > 5) {
            System.out.println("   ⚠️  Makespan改进中等 (5-15%)，CacheAware_v2算法有一定优势");
        } else {
            System.out.println("   ❌ Makespan改进较小 (<5%)，算法优势不够明显");
        }
        
        if (winRate > 75) {
            System.out.println("   ✅ 胜率优秀 (>75%)，CacheAware_v2算法稳定性好");
        } else if (winRate > 60) {
            System.out.println("   ⚠️  胜率良好 (60-75%)，CacheAware_v2算法较为稳定");
        } else {
            System.out.println("   ❌ 胜率偏低 (<60%)，算法性能不够稳定");
        }
        
        System.out.println("=".repeat(80));
    }
    
    /**
     * 生成可视化图表
     */    /**
     * 生成可视化图表和数据分析
     */
    private static void generateVisualization(Map<String, List<ExperimentResult>> algorithmResults) {
        System.out.println("\n📊 生成可视化图表和数据分析...");
          try {
            // 确保result目录存在
            File resultDir = new File("result");
            if (!resultDir.exists()) {
                resultDir.mkdirs();
                System.out.println("📁 创建结果目录: result");
            }
            
            // 生成ASCII图表
            visualizer.generatePerformanceChart(algorithmResults);
            
            // 导出CSV数据到result目录
            String csvFilename = "result/algorithm_comparison_results.csv";
            visualizer.exportToCSV(algorithmResults, csvFilename);
            System.out.println("✅ 实验数据已导出到: " + csvFilename);
            
            // 生成对比矩阵
            visualizer.generateComparisonMatrix(algorithmResults);
            
            // 调用Python脚本处理CSV数据
            runPythonVisualization(csvFilename);
            
        } catch (Exception e) {
            System.err.println("❌ 可视化生成失败: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * 调用Python脚本处理CSV数据并生成高级可视化
     */
    private static void runPythonVisualization(String csvPath) {
        System.out.println("\n🐍 启动Python数据可视化处理...");
        
        try {
            // 检查Python环境：优先 python3，回退 python（修正原逻辑把成功命令选反的缺陷）
            String pythonCommand = null;
            for (String candidate : new String[]{"python3", "python"}) {
                try {
                    Process checkProcess =
                            new ProcessBuilder(candidate, "--version").directory(new File(".")).start();
                    if (checkProcess.waitFor() == 0) {
                        pythonCommand = candidate;
                        break;
                    }
                } catch (Exception ignore) {
                    // 该候选命令不存在，继续尝试下一个
                }
            }

            if (pythonCommand == null) {
                System.out.println("⚠️  未找到 python3/python，跳过Python可视化（不影响实验结果）");
                return;
            }

            // 运行Python可视化脚本；继承标准错误以便观察脚本报错
            ProcessBuilder pb = new ProcessBuilder(pythonCommand, "visualizer/visualize.py", csvPath);
            pb.directory(new File("."));
            pb.redirectErrorStream(true);
            Process process = pb.start();
            // 实时转发脚本输出（避免静默失败导致“无图表”难以察觉）
            try (java.io.BufferedReader reader =
                         new java.io.BufferedReader(new java.io.InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    System.out.println("   [viz] " + line);
                }
            }
            int result = process.waitFor();
            if (result != 0) {
                System.err.println("❌ Python可视化脚本以非零码退出: " + result
                        + "（CSV 已生成，可手动重跑）");
            }
        } catch (Exception e) {
            System.err.println("❌ Python可视化处理失败: " + e.getMessage());
            System.out.println("💡 可以手动运行: python3 visualizer/visualize.py " + csvPath);
        }
    }
}
