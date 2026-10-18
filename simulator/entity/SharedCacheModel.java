package entity;

import java.util.ArrayList;
import java.util.List;

/**
 * 共享缓存 / 执行时间模型（公平基线核心）。
 *
 * <p><b>为什么需要它？</b> 在旧实现中，WFD 与 CacheAware 走的是两套互不相干的模型：
 * <ul>
 *   <li>{@code simulateCacheAwareAllocation} 用 {@code CacheAware_v2.calculateExecutionTime()}
 *       算出一个"打了折"的执行时间，并且直接覆盖 {@code task.expectedET}；</li>
 *   <li>{@code simulateWFDAllocation} 直接用原始 {@code expectedET}，完全没有缓存收益；</li>
 *   <li>{@code PerformanceAnalyzer} 再按算法名分别套用两套手写的命中率估算公式，
 *       其中 WFD 那套还被硬编码截断到 [0.30, 0.70]。</li>
 * </ul>
 * 于是"CacheAware 更快、命中率更高"部分是由模型口径差异直接制造出来的，而不是调度决策的结果。
 *
 * <p><b>本类的职责</b>：作为**唯一**的执行时间 / 缓存命中判定来源。两种算法只允许在
 * "把任务放到哪个处理器"这一件事上不同，放置完成之后一律调用 {@link #execute(Node, int)}
 * 得到执行时间与命中情况。模型参数与 {@link RecencyProfileReal} 保持一致：
 * <ul>
 *   <li>L1 命中 → 基准时间 ×0.8</li>
 *   <li>L2 命中 → ×0.9（L2 由 {@code l2GroupSize} 个相邻核心共享）</li>
 *   <li>L3 命中 → ×0.95（全芯片共享）</li>
 *   <li>全部 miss → ×1.2</li>
 *   <li>任务带故障 → 再 ×1.5</li>
 * </ul>
 *
 * <p><b>命中判定</b>：与 {@code RecencyProfileReal#hasRecentAccess} 相同——在最近
 * {@value #RECENT_WINDOW} 次访问中查找同 {@code dagID} 的任务。也就是说"同一个 DAG 的相邻任务
 * 在同一个核心上连续执行"才算命中，这正是缓存亲和性调度应当利用的局部性。
 *
 * <p><b>写回字段</b>：{@code measuredCacheLevel}、{@code cacheMeasured}、
 * {@code actualCacheHitRatio}、{@code actualL1L2L3HitRatio}。分析器只认这些实测值。
 *
 * <p>本类非线程安全；每个测试用例应新建（或用 {@link #reset()} 复位）一个实例。
 */
public class SharedCacheModel {

    /** 命中判定所回看的最近访问次数（与 RecencyProfileReal 一致）。 */
    public static final int RECENT_WINDOW = 5;

    /** 各级命中相对基准执行时间的系数。 */
    public static final double L1_FACTOR = 0.8;
    public static final double L2_FACTOR = 0.9;
    public static final double L3_FACTOR = 0.95;
    public static final double MEM_FACTOR = 1.2;

    /** 故障任务的执行时间放大系数。 */
    public static final double FAULT_FACTOR = 1.5;

    private final int cores;
    private final int l2GroupSize;

    /** 每核心的 L1 访问历史。 */
    private final List<List<Node>> l1History;

    /** 每 L2 共享组的访问历史（组下标 = processor / l2GroupSize）。 */
    private final List<List<Node>> l2History;

    /** 全芯片共享的 L3 访问历史。 */
    private final List<Node> l3History;

    /**
     * @param cores       处理器数量
     * @param l2GroupSize 每多少个核心共享一个 L2（需 >= 1）
     */
    public SharedCacheModel(int cores, int l2GroupSize) {
        if (cores <= 0) {
            throw new IllegalArgumentException("cores 必须为正数: " + cores);
        }
        this.cores = cores;
        this.l2GroupSize = Math.max(1, l2GroupSize);

        this.l1History = new ArrayList<>(cores);
        for (int i = 0; i < cores; i++) {
            this.l1History.add(new ArrayList<>());
        }

        int groups = (cores + this.l2GroupSize - 1) / this.l2GroupSize;
        this.l2History = new ArrayList<>(groups);
        for (int i = 0; i < groups; i++) {
            this.l2History.add(new ArrayList<>());
        }

        this.l3History = new ArrayList<>();
    }

    /** 清空所有缓存历史（等价于冷启动）。 */
    public void reset() {
        for (List<Node> h : l1History) {
            h.clear();
        }
        for (List<Node> h : l2History) {
            h.clear();
        }
        l3History.clear();
    }

    public int getCores() {
        return cores;
    }

    public int getL2GroupSize() {
        return l2GroupSize;
    }

    /**
     * 在指定处理器上"执行"任务：判定缓存级别、计算执行时间，并把实测结果写回任务。
     *
     * <p>调用方负责根据返回值推进时间轴（{@code start}/{@code finishAt}）。
     * 本方法**不会**修改 {@code task.expectedET}——基准执行时间必须保持不变，
     * 否则两种算法看到的"基准"就不同了。
     *
     * @param task      要执行的任务（需已设置 {@code expectedET}）
     * @param processor 执行该任务的处理器编号
     * @return 考虑缓存命中后的实际执行时间（纳秒）
     */
    public long execute(Node task, int processor) {
        if (task == null) {
            throw new IllegalArgumentException("task 不能为 null");
        }
        if (processor < 0 || processor >= cores) {
            throw new IllegalArgumentException(
                    "processor 越界: " + processor + "（应在 [0, " + cores + ")）");
        }

        int level = resolveCacheLevel(task, processor);
        long base = Math.max(0L, task.expectedET);
        long et = applyLevelFactor(base, level);
        if (task.hasFaults) {
            et = (long) (et * FAULT_FACTOR);
        }
        // 保持与旧模型一致的边界：真任务不应因为取整变成 0，也不允许净加速超过 50%。
        if (base > 0 && et <= 0) {
            et = 1;
        }
        long floor = base / 2;
        if (et < floor) {
            et = floor;
        }

        recordMeasurements(task, level);
        updateHistory(task, processor, level);
        return et;
    }

    /** 只判定缓存级别，不产生副作用（便于单测与调试）。 */
    public int resolveCacheLevel(Node task, int processor) {
        if (hasRecentAccess(l1History.get(processor), task)) {
            return 1;
        }
        if (hasRecentAccess(l2History.get(processor / l2GroupSize), task)) {
            return 2;
        }
        if (hasRecentAccess(l3History, task)) {
            return 3;
        }
        return 4;
    }

    /** 按缓存级别换算执行时间。 */
    public static long applyLevelFactor(long base, int level) {
        double factor;
        switch (level) {
            case 1:
                factor = L1_FACTOR;
                break;
            case 2:
                factor = L2_FACTOR;
                break;
            case 3:
                factor = L3_FACTOR;
                break;
            default:
                factor = MEM_FACTOR;
                break;
        }
        return (long) (base * factor);
    }

    /**
     * 把"命中了哪一级"折算成加权命中率。
     *
     * <p>语义：一次访问若在 L1 命中，则 L1/L2/L3 三级都算命中（数据必然也存在于更外层）；
     * 若在 L2 命中，则 L1 未命中而 L2/L3 命中；依此类推。再按任务的
     * {@code weights} = [L1, L2, L3, Mem] 加权，得到 0..1 的整体命中率。
     */
    public static double weightedHitRatio(Node task, int level) {
        boolean hitL1 = level <= 1;
        boolean hitL2 = level <= 2;
        boolean hitL3 = level <= 3;

        double[] w = normalizedWeights(task);
        return w[0] * (hitL1 ? 1.0 : 0.0)
                + w[1] * (hitL2 ? 1.0 : 0.0)
                + w[2] * (hitL3 ? 1.0 : 0.0);
        // w[3]（内存）不贡献命中率
    }

    /**
     * 归一化任务权重（缺失或全零时退化为均匀的 L1 主导分布）。
     * 返回长度 4 的新数组，调用方可安全修改。
     */
    static double[] normalizedWeights(Node task) {
        double[] w = new double[4];
        if (task.weights != null && task.weights.length >= 3) {
            w[0] = Math.max(0.0, task.weights[0]);
            w[1] = Math.max(0.0, task.weights[1]);
            w[2] = Math.max(0.0, task.weights[2]);
            w[3] = task.weights.length >= 4 ? Math.max(0.0, task.weights[3]) : 0.0;
        }
        double sum = w[0] + w[1] + w[2] + w[3];
        if (sum <= 0.0) {
            // 没有权重信息时按"全部访问都落到 L1"处理，避免除零。
            return new double[] {1.0, 0.0, 0.0, 0.0};
        }
        for (int i = 0; i < w.length; i++) {
            w[i] /= sum;
        }
        return w;
    }

    private void recordMeasurements(Node task, int level) {
        task.cacheMeasured = true;
        task.measuredCacheLevel = level;
        task.actualCacheHitRatio = weightedHitRatio(task, level);
        task.actualL1L2L3HitRatio[0] = level <= 1 ? 1.0 : 0.0;
        task.actualL1L2L3HitRatio[1] = level <= 2 ? 1.0 : 0.0;
        task.actualL1L2L3HitRatio[2] = level <= 3 ? 1.0 : 0.0;
    }

    /** 按层级把任务写入历史；每层只保留最近 RECENT_WINDOW 条，避免无界增长。 */
    private void updateHistory(Node task, int processor, int level) {
        if (level <= 1) {
            touch(l1History.get(processor), task);
        }
        if (level <= 2) {
            touch(l2History.get(processor / l2GroupSize), task);
        }
        if (level <= 3) {
            touch(l3History, task);
        } else {
            // 完全 miss：数据被载入，仍然会占满 L1/L2/L3，因此三层历史都要更新。
            touch(l1History.get(processor), task);
            touch(l2History.get(processor / l2GroupSize), task);
            touch(l3History, task);
        }
    }

    private static void touch(List<Node> history, Node task) {
        history.add(task);
        if (history.size() > RECENT_WINDOW) {
            history.remove(0);
        }
    }

    /** 与 RecencyProfileReal#hasRecentAccess 完全一致的判定口径。 */
    private static boolean hasRecentAccess(List<Node> history, Node node) {
        int count = 0;
        for (int i = history.size() - 1; i >= 0 && count < RECENT_WINDOW; i--, count++) {
            if (history.get(i).getDagID() == node.getDagID()) {
                return true;
            }
        }
        return false;
    }
}
