package visualizer;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.text.DecimalFormat;
import java.util.List;
import java.util.Map;

import analyzer.PerformanceAnalyzer.ExperimentResult;

/**
 * 实验结果可视化器
 * 
 * 该类负责将算法性能对比的实验结果以直观的方式展示出来。
 * 提供ASCII图表生成、CSV数据导出和性能总结分析功能。
 * 
 * 主要功能：
 * - 生成ASCII条形图进行性能对比
 * - 导出CSV格式的实验数据
 * - 计算和显示性能改进摘要
 * - 支持多算法对比分析
 * - 生成趋势分析图表
 * 
 * 支持的可视化类型：
 * - Makespan对比图表
 * - 缓存命中率对比图表
 * - 负载均衡度对比图表
 * - 性能改进总结
 * 
 * @author Cache-Aware Task Scheduling System
 */
public class ResultVisualizer {
    
    /** 数字格式化器，用于结果显示 */
    private DecimalFormat df = new DecimalFormat("#.##");      /**
     * 生成ASCII性能对比图表
     * 
     * 该方法创建直观的文本图表来比较不同算法的性能表现。
     * 包括makespan、缓存命中率、负载均衡度、执行时间等关键指标的可视化对比。
     * 
     * @param algorithmResults 包含各算法实验结果的映射表
     */
    public void generatePerformanceChart(Map<String, List<ExperimentResult>> algorithmResults) {
        System.out.println("\n=== 性能对比图表 ===");
        
        String[] algorithms = algorithmResults.keySet().toArray(new String[0]);
        if (algorithms.length < 2) return;
        
        // 计算各算法的平均性能指标
        double[] avgMakespan = new double[algorithms.length];
        double[] avgEnergy = new double[algorithms.length];
        double[] avgCacheHit = new double[algorithms.length];
        double[] avgLoadBalance = new double[algorithms.length];
        double[] avgResponseTime = new double[algorithms.length];
        double[] avgTotalExecTime = new double[algorithms.length];
        double[] avgTaskExecTime = new double[algorithms.length];
        
        for (int i = 0; i < algorithms.length; i++) {
            List<ExperimentResult> results = algorithmResults.get(algorithms[i]);
            avgMakespan[i] = results.stream().mapToDouble(r -> r.makespan).average().orElse(0);
            avgEnergy[i] = results.stream().mapToDouble(r -> r.energyConsumption).average().orElse(0);
            avgCacheHit[i] = results.stream().mapToDouble(r -> r.cacheHitRatio).average().orElse(0);
            avgLoadBalance[i] = results.stream().mapToDouble(r -> r.loadBalance).average().orElse(0);
            avgResponseTime[i] = results.stream().mapToDouble(r -> r.averageResponseTime).average().orElse(0);
            avgTotalExecTime[i] = results.stream().mapToDouble(r -> r.totalTaskExecutionTime).average().orElse(0);
            avgTaskExecTime[i] = results.stream().mapToDouble(r -> r.averageTaskExecutionTime).average().orElse(0);
        }
        
        // 生成完成时间对比图
        System.out.println("\n【Makespan对比】(时间单位)");
        double maxMakespan = Math.max(avgMakespan[0], avgMakespan[1]);
        for (int i = 0; i < algorithms.length; i++) {
            String bar = generateBar(avgMakespan[i], maxMakespan, 40);
            System.out.printf("%-15s: %s %.2f\n", algorithms[i], bar, avgMakespan[i]);
        }
        
        // 生成完整任务执行时间对比图
        System.out.println("\n【完整任务执行时间对比】(时间单位)");
        double maxTotalExec = Math.max(avgTotalExecTime[0], avgTotalExecTime[1]);
        for (int i = 0; i < algorithms.length; i++) {
            String bar = generateBar(avgTotalExecTime[i], maxTotalExec, 40);
            System.out.printf("%-15s: %s %.2f\n", algorithms[i], bar, avgTotalExecTime[i]);
        }
        
        // 显示执行时间改进
        if (avgTotalExecTime[0] != 0 && avgTotalExecTime[1] != 0) {
            double improvement = ((avgTotalExecTime[0] - avgTotalExecTime[1]) / avgTotalExecTime[0]) * 100;
            System.out.printf("📈 执行时间改进: %.2f%%\n", improvement);
        }
        
        // 生成平均任务执行时间对比图
        System.out.println("\n【平均任务执行时间对比】(时间单位)");
        double maxTaskExec = Math.max(avgTaskExecTime[0], avgTaskExecTime[1]);
        for (int i = 0; i < algorithms.length; i++) {
            String bar = generateBar(avgTaskExecTime[i], maxTaskExec, 40);
            System.out.printf("%-15s: %s %.2f\n", algorithms[i], bar, avgTaskExecTime[i]);
        }
        
        // 生成缓存命中率对比图
        System.out.println("\n【缓存命中率对比】(百分比)");
        double maxCacheHit = Math.max(avgCacheHit[0], avgCacheHit[1]);
        for (int i = 0; i < algorithms.length; i++) {
            String bar = generateBar(avgCacheHit[i], maxCacheHit, 40);
            System.out.printf("%-15s: %s %.1f%%\n", algorithms[i], bar, avgCacheHit[i] * 100);
        }
        
        // 生成负载均衡对比图
        System.out.println("\n【负载均衡度对比】(0-1,越高越好)");
        double maxLoadBalance = Math.max(avgLoadBalance[0], avgLoadBalance[1]);
        for (int i = 0; i < algorithms.length; i++) {
            String bar = generateBar(avgLoadBalance[i], maxLoadBalance, 40);
            System.out.printf("%-15s: %s %.3f\n", algorithms[i], bar, avgLoadBalance[i]);
        }
        
        generateImprovementSummary(algorithms, avgMakespan, avgCacheHit, avgLoadBalance, 
                                 avgResponseTime, avgTotalExecTime, avgTaskExecTime);
    }
    
    private String generateBar(double value, double max, int maxLength) {
        if (max == 0) return "";
        int barLength = (int) ((value / max) * maxLength);
        StringBuilder bar = new StringBuilder();
        for (int i = 0; i < barLength; i++) bar.append("█");
        for (int i = barLength; i < maxLength; i++) bar.append("░");
        return bar.toString();
    }    private void generateImprovementSummary(String[] algorithms, double[] makespan, 
            double[] cacheHit, double[] loadBalance, double[] responseTime,
            double[] totalExecTime, double[] avgExecTime) {
        System.out.println("\n=== 性能改进总结 ===");
        
        if (algorithms.length >= 2) {
            String algo1 = algorithms[0];
            String algo2 = algorithms[1];
            
            System.out.println("📊 " + algo1 + " vs " + algo2 + " 综合对比:");
            
            // 说明：algo1（Cache-Aware）为待评估算法，algo2（WFD）为基准算法。
            // 越低越优的指标（执行时间 / Makespan）以 algo2 为分母计算相对改进，
            // 正值表示 algo1 优于基准；负值表示劣于基准。
            System.out.printf("  （基准: %s，正值表示 %s 更优）%n", algo2, algo1);

            // 执行时间改进
            if (totalExecTime[1] != 0) {
                double improvement = ((totalExecTime[1] - totalExecTime[0]) / totalExecTime[1]) * 100;
                System.out.printf("  ⏱️  完整任务执行时间: %s%.2f%%\n",
                    improvement >= 0 ? "改进 " : "劣化 ", Math.abs(improvement));
            }

            if (avgExecTime[1] != 0) {
                double improvement = ((avgExecTime[1] - avgExecTime[0]) / avgExecTime[1]) * 100;
                System.out.printf("  ⏰ 平均任务执行时间: %s%.2f%%\n",
                    improvement >= 0 ? "改进 " : "劣化 ", Math.abs(improvement));
            }

            // Makespan改进
            if (makespan[1] != 0) {
                double improvement = ((makespan[1] - makespan[0]) / makespan[1]) * 100;
                System.out.printf("  📈 Makespan: %s%.2f%%\n",
                    improvement >= 0 ? "改进 " : "劣化 ", Math.abs(improvement));
            }

            // 缓存命中率改进（越高越优，直接作差）
            double cacheImprovement = (cacheHit[0] - cacheHit[1]) * 100;
            System.out.printf("  🗄️  缓存命中率: %s%.2f 个百分点\n",
                cacheImprovement >= 0 ? "提升 " : "降低 ", Math.abs(cacheImprovement));
        }
    }
    
    /**
     * CSV 列定义（schema 的事实来源）。
     *
     * <p><b>这是一个契约，不是实现细节。</b>列名与顺序被以下消费者按名读取：
     * {@code visualizer/visualize.py}、{@code docs/EXPERIMENT_REPORT.md} 里记录的历史结果，
     * 以及任何下游脚本。改动列名/顺序等同于破坏性变更，必须同步：
     * <ol>
     *   <li>本常量</li>
     *   <li>{@link #CSV_FORMAT} 的占位符与顺序</li>
     *   <li>{@link #CSV_SCHEMA_VERSION}</li>
     *   <li>{@code docs/SCHEMA.md} 的字段表</li>
     *   <li>{@code visualize.py} 的列名引用</li>
     * </ol>
     */
    public static final String[] CSV_COLUMNS = {
            "Algorithm",            // 算法名：WFD / CacheAware_v2
            "TestCase",             // 案例序号（从 1 开始，跨利用率级别连续）
            "Makespan",             // 总完成时间
            "CacheHit",             // 实测缓存命中率 [0,1]
            "TotalExecutionTime",   // 该案例所有任务执行时间之和
            "AvgExecutionTime",     // 单任务平均执行时间
            "ResponseTime",         // 平均响应时间
            "CpuUtilization",       // CPU 利用率 [0,1]
            "LoadBalance"           // 负载均衡度 [0,1]，越高越均衡
    };

    /** 与 {@link #CSV_COLUMNS} 严格等长的格式串。 */
    private static final String CSV_FORMAT = "%s,%d,%.2f,%.3f,%.2f,%.2f,%.2f,%.3f,%.3f%n";

    /**
     * CSV schema 版本。仅在<b>列集合或语义</b>变化时递增；
     * 只改数值精度（格式串里的位数）不递增，因为不破坏按名读取。
     */
    public static final int CSV_SCHEMA_VERSION = 1;

    /** 自检：列数与格式串占位符数必须一致，否则运行期会静默错位。 */
    static {
        int placeholders = 0;
        for (int i = 0; i < CSV_FORMAT.length(); i++) {
            if (CSV_FORMAT.charAt(i) == '%' && i + 1 < CSV_FORMAT.length()
                    && CSV_FORMAT.charAt(i + 1) != '%' && CSV_FORMAT.charAt(i + 1) != 'n') {
                placeholders++;
            }
        }
        if (placeholders != CSV_COLUMNS.length) {
            throw new IllegalStateException("CSV_COLUMNS 有 " + CSV_COLUMNS.length
                    + " 列，但 CSV_FORMAT 有 " + placeholders + " 个占位符");
        }
    }

    public void generateTrendChart(Map<String, List<ExperimentResult>> algorithmResults) {
        System.out.println("\n=== 性能趋势分析 ===");
    }    public void exportToCSV(Map<String, List<ExperimentResult>> algorithmResults, String filename) {
        try {
            // 创建目录（如果不存在）
            File file = new File(filename);
            File parentDir = file.getParentFile();
            if (parentDir != null && !parentDir.exists()) {
                parentDir.mkdirs();
            }
            
            // 写入CSV文件，包含执行时间数据
            try (FileWriter writer = new FileWriter(filename)) {
                writer.write(String.join(",", CSV_COLUMNS));
                writer.write('\n');
                for (String algorithm : algorithmResults.keySet()) {
                    for (ExperimentResult result : algorithmResults.get(algorithm)) {
                        writer.write(String.format(CSV_FORMAT,
                            algorithm,
                            result.testCaseId,
                            result.makespan,
                            result.cacheHitRatio,
                            result.totalTaskExecutionTime,
                            result.averageTaskExecutionTime,
                            result.averageResponseTime,
                            result.cpuUtilization,
                            result.loadBalance));
                    }
                }
                System.out.println("✅ CSV数据已导出到: " + filename);
            }
            writeMetadataSidecar(filename, algorithmResults);
        } catch (IOException e) {
            System.err.println("❌ CSV导出失败: " + e.getMessage());
        }
    }
    /**
     * 在 CSV 旁边写一个 {@code <name>.meta.json} 元数据旁文件。
     *
     * <p>为什么不做「在 CSV 前面加几行注释」：CSV 没有标准注释语法，
     * 多加的 `#` 行会被 {@code pandas.read_csv} 当成数据行，直接破坏既有消费者。
     * 旁文件因此是最小侵入的选择。
     *
     * <p>旁文件记录的是<b>「这份 CSV 是在什么条件下产生的」</b>：
     * schema 版本、列名、算法集合、行数。它<b>不</b>记录实验参数——
     * 参数属于 {@code config/*.yaml}，重复记录会造成两处事实互相矛盾。
     *
     * <p>失败不致命：CSV 已经写好，旁文件只影响可追溯性，因此这里把异常降级为警告。
     */
    private void writeMetadataSidecar(String csvFilename,
                                      Map<String, List<ExperimentResult>> algorithmResults) {
        File csv = new File(csvFilename);
        String metaName = csvFilename + ".meta.json";
        int rows = 0;
        for (List<ExperimentResult> list : algorithmResults.values()) {
            rows += list.size();
        }

        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append("  \"csv_schema_version\": ").append(CSV_SCHEMA_VERSION).append(",\n");
        sb.append("  \"csv_file\": \"").append(jsonEscape(csv.getName())).append("\",\n");
        sb.append("  \"columns\": [");
        for (int i = 0; i < CSV_COLUMNS.length; i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append('"').append(jsonEscape(CSV_COLUMNS[i])).append('"');
        }
        sb.append("],\n");
        sb.append("  \"algorithms\": [");
        boolean first = true;
        for (String algo : algorithmResults.keySet()) {
            if (!first) {
                sb.append(", ");
            }
            sb.append('"').append(jsonEscape(algo)).append('"');
            first = false;
        }
        sb.append("],\n");
        sb.append("  \"data_rows\": ").append(rows).append(",\n");
        sb.append("  \"note\": \"实验参数见 config/*.yaml；本文件只描述 CSV 结构\"\n");
        sb.append("}\n");

        try (FileWriter w = new FileWriter(metaName)) {
            w.write(sb.toString());
            System.out.println("🧾 元数据旁文件已导出到: " + metaName);
        } catch (IOException e) {
            System.err.println("⚠️  元数据旁文件写入失败（CSV 本身已正常）: " + e.getMessage());
        }
    }

    /** JSON 字符串转义（仅处理会破坏 JSON 结构的最小集合）。 */
    private static String jsonEscape(String s) {
        if (s == null) {
            return "";
        }
        StringBuilder out = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"':  out.append("\\\""); break;
                case '\\': out.append("\\\\"); break;
                case '\n': out.append("\\n");  break;
                case '\r': out.append("\\r");  break;
                case '\t': out.append("\\t");  break;
                default:
                    if (c < 0x20) {
                        out.append(String.format("\\u%04x", (int) c));
                    } else {
                        out.append(c);
                    }
            }
        }
        return out.toString();
    }

    public void generateComparisonMatrix(Map<String, List<ExperimentResult>> algorithmResults) {
        // 移除对比矩阵输出，由主程序控制输出格式
    }
}
