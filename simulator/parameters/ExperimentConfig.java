package parameters;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * 实验参数的外置配置（B3）。
 *
 * <h3>为什么要把参数搬出代码</h3>
 * 之前 {@code CORES} / {@code TOTAL_TEST_CASES} / {@code cc_weights} 等既有
 * 命令行参数、又有 {@code private static final} 常量、还有 {@code setupSystemParameters()}
 * 里的硬编码字面量，三处并存且互相覆盖，导致：
 * <ul>
 *   <li>复现一篇论文的某组参数要改源码重新编译；</li>
 *   <li>「当前跑的是什么参数」只能从 stdout 里反推；</li>
 *   <li>调参靠 git diff，无法把两套参数并存对比。</li>
 * </ul>
 * 现在统一为「配置文件 + 命令行覆盖」，并保证<b>优先级明确</b>：
 * <pre>
 *   代码内默认值  <  config/*.yaml  <  命令行参数
 * </pre>
 *
 * <h3>兼容性约束（硬性）</h3>
 * 若找不到配置文件，必须回落到<b>与改动前完全一致</b>的内置默认值，
 * 使 {@code make run-quick} 等既有入口行为不变。因此这里的默认值逐项对齐
 * 原 {@code AlgorithmComparisonExperiment} 的常量与原 {@code setupSystemParameters()} 的赋值。
 *
 * <h3>行为上的两处语义收紧</h3>
 * <ol>
 *   <li><b>未知键直接报错</b>。配置里写错一个字母（{@code utilisation_levels}）
 *       若被静默忽略，实验会在「以为改了参数」的情况下照旧跑，这是最难查的一类错误。</li>
 *   <li><b>未知的顶层段/嵌套段也算未知键</b>。不做宽松匹配。</li>
 * </ol>
 *
 * <h3>字段语义</h3>
 * <ul>
 *   <li>{@link #ccWeights} 长度为 4，顺序为 {@code [L1, L2, L3, Memory]}，
 *       与 {@link SystemParameters#cc_weights} 一致。</li>
 *   <li>{@link #seeds} 为 {@code null} 表示走单种子路径（{@link #seed}）。</li>
 *   <li>{@link #utilizationLevels} 顺序即实验执行顺序，允许重复、
 *       允许不单调（便于做定向重载测试）。</li>
 * </ul>
 */
public final class ExperimentConfig {

    /** 本文件能理解的配置结构版本。结构不兼容时递增，旧文件会被明确拒绝。 */
    public static final int SCHEMA_VERSION = 1;

    /** 配置文件未指定时的内置默认值（与原硬编码完全一致）。 */
    public static final int DEFAULT_CORES = 8;
    public static final int DEFAULT_TOTAL_TEST_CASES = 100;
    public static final int DEFAULT_TASKS_PER_CASE = 60;
    public static final long DEFAULT_SEED = 42L;
    public static final boolean DEFAULT_ENABLE_VIZ = true;
    public static final boolean DEFAULT_LEGACY_METRICS = false;
    public static final double[] DEFAULT_UTILIZATION_LEVELS = {0.6, 0.8, 1.0, 1.2, 1.5};
    public static final double DEFAULT_HIGH_CACHE_SENSITIVITY_RATIO = 0.7;
    public static final int DEFAULT_CACHE_LEVEL = 3;
    public static final int DEFAULT_LEVEL2_CORE_NUM = 2;
    public static final double[] DEFAULT_CC_WEIGHTS = {0.5, 0.3, 0.15, 0.05};

    /** 配置来源描述，用于日志与报错（真实路径 / {@code <built-in defaults>}）。 */
    private String source = "<built-in defaults>";

    /** 被显式设置过的键（配置文件或命令行），用于「哪些是默认值」的自省输出。 */
    private final Set<String> explicitKeys = new LinkedHashSet<>();

    // ---------- experiment ----------
    public int cores = DEFAULT_CORES;
    public int totalTestCases = DEFAULT_TOTAL_TEST_CASES;
    public int tasksPerCase = DEFAULT_TASKS_PER_CASE;
    public long seed = DEFAULT_SEED;
    public long[] seeds = null;
    public boolean enableViz = DEFAULT_ENABLE_VIZ;
    public boolean legacyMetrics = DEFAULT_LEGACY_METRICS;
    public double[] utilizationLevels = DEFAULT_UTILIZATION_LEVELS.clone();
    public double highCacheSensitivityRatio = DEFAULT_HIGH_CACHE_SENSITIVITY_RATIO;

    // ---------- system ----------
    public int cacheLevel = DEFAULT_CACHE_LEVEL;
    public int level2CoreNum = DEFAULT_LEVEL2_CORE_NUM;
    public double[] ccWeights = DEFAULT_CC_WEIGHTS.clone();

    /** 参数错误（未知键 / 类型不符 / 取值范围非法 / schema 不兼容）。 */
    public static final class ConfigException extends RuntimeException {
        private static final long serialVersionUID = 1L;

        public ConfigException(String message) {
            super(message);
        }
    }

    private ExperimentConfig() {
    }

    // ==================== 入口 ====================

    /** 内置默认配置（不读文件）。 */
    public static ExperimentConfig defaults() {
        return new ExperimentConfig();
    }

    /**
     * 从一个文件加载；文件通过 {@code config} 键指定的部分覆盖内置默认值。
     *
     * @param file 已存在的 YAML 文件
     * @throws IOException    读取失败
     * @throws ConfigException 结构/取值不合法
     */
    public static ExperimentConfig load(Path file) throws IOException {
        Map<String, Object> root;
        try {
            root = YamlSubset.parse(file);
        } catch (YamlSubset.YamlException e) {
            throw e; // 已带 文件:行号
        }
        ExperimentConfig cfg = new ExperimentConfig();
        cfg.source = file.toString();
        cfg.applyRoot(root);
        cfg.validate();
        return cfg;
    }

    /**
     * 按约定顺序寻找配置文件并加载；全都找不到时返回内置默认值。
     *
     * <p>查找顺序：
     * <ol>
     *   <li>显式 {@code --config=PATH}（由调用方预先解析后传入 {@code explicit}）</li>
     *   <li>环境变量 {@code YAT_SIM_CONFIG}</li>
     *   <li>{@code ./config/default.yaml}（相对当前工作目录，即 {@code simulator/}）</li>
     *   <li>相对本类所在位置推导的 {@code ../config/default.yaml}</li>
     * </ol>
     *
     * @param explicit 显式路径；{@code null} 表示未指定
     * @return 配置实例；找不到任何文件时为内置默认，且 {@link #getSource()} 可辨识
     */
    public static ExperimentConfig discover(Path explicit) throws IOException {
        List<Path> candidates = new ArrayList<>();
        if (explicit != null) {
            candidates.add(explicit);
        }
        String env = System.getenv("YAT_SIM_CONFIG");
        if (env != null && !env.trim().isEmpty()) {
            candidates.add(Paths.get(env.trim()));
        }
        candidates.add(Paths.get("config", "default.yaml"));
        candidates.add(Paths.get("simulator", "config", "default.yaml"));

        if (explicit != null) {
            // 显式指定却不存在，属于用户错误，不能静默回落
            if (!Files.isRegularFile(explicit)) {
                throw new ConfigException("指定的配置文件不存在: " + explicit);
            }
            return load(explicit);
        }
        for (Path candidate : candidates) {
            if (Files.isRegularFile(candidate)) {
                return load(candidate);
            }
        }
        ExperimentConfig cfg = new ExperimentConfig();
        cfg.source = "<built-in defaults: 未找到 config/default.yaml>";
        return cfg;
    }

    // ==================== 解析 ====================

    private void applyRoot(Map<String, Object> root) {
        Set<String> allowedTop = new LinkedHashSet<>(Arrays.asList("schema_version", "experiment", "system"));
        for (String key : root.keySet()) {
            if (!allowedTop.contains(key)) {
                throw new ConfigException(source + ": 未知的顶层键 '" + key
                        + "'（可用: " + allowedTop + "）");
            }
        }

        Object versionNode = root.get("schema_version");
        if (versionNode == null) {
            throw new ConfigException(source + ": 缺少必填键 schema_version"
                    + "（当前代码支持 " + SCHEMA_VERSION + "）");
        }
        int version = asInt("schema_version", versionNode);
        if (version != SCHEMA_VERSION) {
            throw new ConfigException(source + ": schema_version=" + version
                    + "，但本代码只支持 " + SCHEMA_VERSION
                    + "；结构已变更，请参照 config/default.yaml 更新");
        }

        // 两个段都是可选的：一个只想调 cores 的 profile 不应该被迫把 system 段抄一遍。
        // 「段内键」仍然严格（未知键报错），只是段本身允许缺席。
        if (root.containsKey("experiment")) {
            applyExperiment(section(root, "experiment"));
        }
        if (root.containsKey("system")) {
            applySystem(section(root, "system"));
        }
    }

    private void applyExperiment(Map<String, Object> s) {
        Set<String> allowed = new LinkedHashSet<>(Arrays.asList(
                "cores", "total_test_cases", "tasks_per_case", "seed", "seeds",
                "enable_viz", "legacy_metrics", "utilization_levels",
                "high_cache_sensitivity_ratio"));
        rejectUnknown("experiment", s, allowed);

        if (s.containsKey("cores")) {
            cores = Math.max(1, asInt("experiment.cores", s.get("cores")));
            explicitKeys.add("experiment.cores");
        }
        if (s.containsKey("total_test_cases")) {
            totalTestCases = Math.max(1, asInt("experiment.total_test_cases", s.get("total_test_cases")));
            explicitKeys.add("experiment.total_test_cases");
        }
        if (s.containsKey("tasks_per_case")) {
            tasksPerCase = Math.max(1, asInt("experiment.tasks_per_case", s.get("tasks_per_case")));
            explicitKeys.add("experiment.tasks_per_case");
        }
        if (s.containsKey("seed")) {
            seed = asLong("experiment.seed", s.get("seed"));
            explicitKeys.add("experiment.seed");
        }
        if (s.containsKey("seeds")) {
            seeds = parseSeedsNode(s.get("seeds"));
            explicitKeys.add("experiment.seeds");
        }
        if (s.containsKey("enable_viz")) {
            enableViz = asBool("experiment.enable_viz", s.get("enable_viz"));
            explicitKeys.add("experiment.enable_viz");
        }
        if (s.containsKey("legacy_metrics")) {
            legacyMetrics = asBool("experiment.legacy_metrics", s.get("legacy_metrics"));
            explicitKeys.add("experiment.legacy_metrics");
        }
        if (s.containsKey("utilization_levels")) {
            utilizationLevels = asDoubleArray("experiment.utilization_levels",
                    s.get("utilization_levels"), 1, -1);
            explicitKeys.add("experiment.utilization_levels");
        }
        if (s.containsKey("high_cache_sensitivity_ratio")) {
            highCacheSensitivityRatio = asDouble("experiment.high_cache_sensitivity_ratio",
                    s.get("high_cache_sensitivity_ratio"));
            explicitKeys.add("experiment.high_cache_sensitivity_ratio");
        }
    }

    private void applySystem(Map<String, Object> s) {
        Set<String> allowed = new LinkedHashSet<>(Arrays.asList(
                "cache_level", "level2_core_num", "cc_weights"));
        rejectUnknown("system", s, allowed);

        if (s.containsKey("cache_level")) {
            cacheLevel = Math.max(1, asInt("system.cache_level", s.get("cache_level")));
            explicitKeys.add("system.cache_level");
        }
        if (s.containsKey("level2_core_num")) {
            level2CoreNum = Math.max(0, asInt("system.level2_core_num", s.get("level2_core_num")));
            explicitKeys.add("system.level2_core_num");
        }
        if (s.containsKey("cc_weights")) {
            ccWeights = asDoubleArray("system.cc_weights", s.get("cc_weights"), 4, 4);
            explicitKeys.add("system.cc_weights");
        }
    }

    /**
     * 语义校验。结构已在解析阶段保证，这里检查「物理上无意义」的取值。
     *
     * <p>刻意不做「自动纠正」（例如把 -1 的比例改成 0）：配置错误应当让实验停下，
     * 而不是产出看起来正常但口径错误的 CSV。
     */
    public void validate() {
        if (cores < 1) {
            throw new ConfigException("experiment.cores 必须 ≥ 1，实际 " + cores);
        }
        if (totalTestCases < 1) {
            throw new ConfigException("experiment.total_test_cases 必须 ≥ 1，实际 " + totalTestCases);
        }
        if (tasksPerCase < 1) {
            throw new ConfigException("experiment.tasks_per_case 必须 ≥ 1，实际 " + tasksPerCase);
        }
        if (utilizationLevels.length == 0) {
            throw new ConfigException("experiment.utilization_levels 不能为空");
        }
        for (double u : utilizationLevels) {
            if (!(u > 0.0) || Double.isInfinite(u)) {
                throw new ConfigException("experiment.utilization_levels 含非法值 " + u
                        + "（须为有限正数）");
            }
        }
        if (!(highCacheSensitivityRatio >= 0.0 && highCacheSensitivityRatio <= 1.0)) {
            throw new ConfigException("experiment.high_cache_sensitivity_ratio 必须在 [0,1]，实际 "
                    + highCacheSensitivityRatio);
        }
        double sum = 0.0;
        for (double w : ccWeights) {
            if (w < 0.0 || Double.isNaN(w)) {
                throw new ConfigException("system.cc_weights 含负值或 NaN: " + w);
            }
            sum += w;
        }
        if (!(sum > 0.0)) {
            throw new ConfigException("system.cc_weights 全为 0，缓存层级权重无意义");
        }
        if (cacheLevel < 1) {
            throw new ConfigException("system.cache_level 必须 ≥ 1，实际 " + cacheLevel);
        }
        if (level2CoreNum < 0) {
            throw new ConfigException("system.level2_core_num 必须 ≥ 0，实际 " + level2CoreNum);
        }
        if (seeds != null) {
            if (seeds.length == 0) {
                throw new ConfigException("experiment.seeds 不能为空列表（用 null 表示单种子路径）");
            }
        }
    }

    // ==================== 命令行覆盖 ====================

    /** 幂等：命令行显式给出即为最高优先级。 */
    public void overrideCores(int v) {
        cores = Math.max(1, v);
        explicitKeys.add("experiment.cores");
    }

    public void overrideTotalTestCases(int v) {
        totalTestCases = Math.max(1, v);
        explicitKeys.add("experiment.total_test_cases");
    }

    public void overrideTasksPerCase(int v) {
        tasksPerCase = Math.max(1, v);
        explicitKeys.add("experiment.tasks_per_case");
    }

    public void overrideSeed(long v) {
        seed = v;
        explicitKeys.add("experiment.seed");
    }

    public void overrideSeeds(long[] v) {
        seeds = v;
        explicitKeys.add("experiment.seeds");
    }

    public void overrideEnableViz(boolean v) {
        enableViz = v;
        explicitKeys.add("experiment.enable_viz");
    }

    public void overrideLegacyMetrics(boolean v) {
        legacyMetrics = v;
        explicitKeys.add("experiment.legacy_metrics");
    }

    // ==================== 类型转换 ====================

    private static Map<String, Object> section(Map<String, Object> root, String name) {
        Object node = root.get(name);
        if (node == null) {
            // `system:` 写成带冒号无内容时 YamlSubset 返回空 Map，不会走到这里；
            // 只有键完全缺席才会是 null（此时调用方已用 containsKey 挡住）。
            throw new ConfigException("段 '" + name + ":' 内容为空，请删除该段或填入具体键");
        }
        if (!(node instanceof Map)) {
            throw new ConfigException("'" + name + "' 必须是映射段（" + name + ": 下一层缩进），实际是 "
                    + node.getClass().getSimpleName());
        }
        @SuppressWarnings("unchecked")
        Map<String, Object> map = (Map<String, Object>) node;
        return map;
    }

    private void rejectUnknown(String section, Map<String, Object> map, Set<String> allowed) {
        for (String key : map.keySet()) {
            if (!allowed.contains(key)) {
                throw new ConfigException(source + ": " + section + " 段的未知键 '" + key
                        + "'（可用: " + allowed + "）");
            }
        }
    }

    private int asInt(String key, Object node) {
        if (node instanceof Long) {
            long v = (Long) node;
            if (v < Integer.MIN_VALUE || v > Integer.MAX_VALUE) {
                throw new ConfigException(source + ": " + key + " 超出 int 范围: " + v);
            }
            return (int) v;
        }
        if (node instanceof Double) {
            double d = (Double) node;
            if (d != Math.rint(d)) {
                throw new ConfigException(source + ": " + key + " 需为整数，实际 " + d);
            }
            return (int) d;
        }
        throw new ConfigException(source + ": " + key + " 需为整数，实际 "
                + typeName(node));
    }

    private long asLong(String key, Object node) {
        if (node instanceof Long) {
            return (Long) node;
        }
        throw new ConfigException(source + ": " + key + " 需为整数，实际 " + typeName(node));
    }

    private double asDouble(String key, Object node) {
        if (node instanceof Double) {
            return (Double) node;
        }
        if (node instanceof Long) {
            return ((Long) node).doubleValue();
        }
        throw new ConfigException(source + ": " + key + " 需为数值，实际 " + typeName(node));
    }

    private boolean asBool(String key, Object node) {
        if (node instanceof Boolean) {
            return (Boolean) node;
        }
        throw new ConfigException(source + ": " + key + " 需为 true/false，实际 " + typeName(node));
    }

    private double[] asDoubleArray(String key, Object node, int minLen, int maxLen) {
        if (!(node instanceof List)) {
            throw new ConfigException(source + ": " + key + " 需为列表（[a, b, ...] 或 - 逐项），实际 "
                    + typeName(node));
        }
        List<?> list = (List<?>) node;
        if (list.size() < minLen || (maxLen >= 0 && list.size() > maxLen)) {
            throw new ConfigException(source + ": " + key + " 元素个数为 " + list.size()
                    + "，要求 " + (maxLen < 0 ? "≥" + minLen : minLen + ".." + maxLen));
        }
        double[] out = new double[list.size()];
        for (int i = 0; i < list.size(); i++) {
            out[i] = asDouble(key + "[" + i + "]", list.get(i));
        }
        return out;
    }

    private long[] parseSeedsNode(Object node) {
        if (node == null) {
            return null; // 显式 null → 单种子路径
        }
        if (!(node instanceof List)) {
            throw new ConfigException(source + ": experiment.seeds 需为列表或 null，实际 "
                    + typeName(node));
        }
        List<?> list = (List<?>) node;
        long[] out = new long[list.size()];
        for (int i = 0; i < list.size(); i++) {
            out[i] = asLong("experiment.seeds[" + i + "]", list.get(i));
        }
        return out;
    }

    private static String typeName(Object node) {
        return node == null ? "null" : node.getClass().getSimpleName();
    }

    // ==================== 自省 ====================

    public String getSource() {
        return source;
    }

    /** 是否为默认值（未被配置文件或命令行显式设置）。 */
    public boolean isExplicit(String key) {
        return explicitKeys.contains(key);
    }

    /** 单行摘要，用于实验启动时打印「到底用了什么参数」。 */
    public String describe() {
        StringBuilder sb = new StringBuilder();
        sb.append("配置来源: ").append(source).append('\n');
        sb.append("  cores=").append(cores)
          .append("(默认 ").append(DEFAULT_CORES).append(')')
          .append(", total_test_cases=").append(totalTestCases)
          .append(", tasks_per_case=").append(tasksPerCase).append('\n');
        sb.append("  seed=").append(seeds == null ? String.valueOf(seed) : Arrays.toString(seeds))
          .append(", enable_viz=").append(enableViz)
          .append(", legacy_metrics=").append(legacyMetrics).append('\n');
        sb.append("  utilization_levels=").append(Arrays.toString(utilizationLevels))
          .append(", high_cache_sensitivity_ratio=").append(highCacheSensitivityRatio).append('\n');
        sb.append("  cache_level=").append(cacheLevel)
          .append(", level2_core_num=").append(level2CoreNum)
          .append(", cc_weights=").append(Arrays.toString(ccWeights));
        return sb.toString();
    }

    /** 把配置写回 {@link SystemParameters}（代替原先散落的硬编码赋值）。 */
    public void applyToSystemParameters() {
        SystemParameters.coreNum = cores;
        SystemParameters.cc_weights = ccWeights.clone();
        SystemParameters.cacheLevel = cacheLevel;
        SystemParameters.Level2CoreNum = level2CoreNum;
    }
}
