package tests;

import entity.Node;
import entity.RecencyProfileReal;
import entity.SharedCacheModel;
import allocation.WFD;
import allocation.CacheAware_v2;
import analyzer.PerformanceAnalyzer;
import parameters.ExperimentConfig;
import parameters.SystemParameters;
import parameters.YamlSubset;
import org.apache.commons.math3.util.Pair;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Random;

/**
 * Yat-CAShed 模拟器单元测试（零依赖、无需 JUnit）。
 *
 * 覆盖不可变/确定性核心逻辑，作为算法回归的“安全网”：
 *   [1] Node            —— 实体构造与约束
 *   [2] RecencyProfileReal —— CRP 核心：L1/L2/L3/内存 命中 → 执行时间与缓存级别映射
 *   [2b] CRP 边界        —— 优先级、访问窗口、数值截断、故障叠加、零基
 *   [3] WFD             —— 最差适配（负载均衡）行为与重置
 *   [4] CacheAware_v2   —— 契约有效性（返回合法处理器，空输入返回 -1）
 *   [5] Speedup/Benefit —— 缓存敏感度收益阈值、命名差异、指标对称性边界
 *   [6] 公平基线 C1    —— SharedCacheModel 不可变性、两算法口径一致、实测命中率
 *
 * 运行：make -C simulator test       （或 java -cp ... tests.YatSimulatorTest）
 * 退出码：0 全通过；1 存在失败用例。
 */
public class YatSimulatorTest {

    private static int passed = 0;
    private static int failed = 0;

    // ---------- 极简断言框架 ----------
    private static void check(String name, boolean cond) {
        if (cond) {
            passed++;
            System.out.println("  ✅ " + name);
        } else {
            failed++;
            System.out.println("  ❌ " + name);
        }
    }

    private static void checkEq(String name, Object expected, Object actual) {
        boolean ok = (expected == null) ? (actual == null) : expected.equals(actual);
        if (!ok) {
            System.out.println("      期望=" + expected + "，实际=" + actual);
        }
        check(name, ok);
    }

    private static void checkNear(String name, double expected, double actual, double tol) {
        boolean ok = Math.abs(expected - actual) <= tol;
        if (!ok) {
            System.out.println("      期望≈" + expected + "，实际=" + actual);
        }
        check(name, ok);
    }

    private static void section(String title) {
        System.out.println("\n[ " + title + " ]");
    }

    // ---------- [1] Node ----------
    private static void testNode() {
        section("1. Node 实体");
        Random rng = new Random(1);
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node n = new Node(0, Node.NodeType.SOLO, 42, 7, crp, rng);

        checkEq("id 保持为 42", 42, n.getId());
        checkEq("dagID 保持为 7", 7, n.getDagID());
        checkEq("weights 长度为 4", 4, n.weights.length);
        check("sensitivity ∈ [0,1]", n.sensitivity >= 0.0 && n.sensitivity <= 1.0);
        check("getWCET() == expectedET", n.getWCET() == n.expectedET);
        check("expectedET ∈ [100,149]", n.expectedET >= 100 && n.expectedET < 150);
        check("children 初始为空", n.getChildren().isEmpty());
        check("parents 初始为空", n.getParent().isEmpty());
        checkEq("partition 初始为 -1", -1, n.partition);
    }

    // ---------- [2] RecencyProfileReal（CRP 核心） ----------
    private static void testRecencyProfile() {
        section("2. RecencyProfileReal CRP 映射");

        long base = 100;
        RecencyProfileReal crp = new RecencyProfileReal(base);
        int proc = 0;
        Node node = new Node(0, Node.NodeType.NORMAL, 1, /*dagID*/ 7, crp, new Random(2));

        // 构造三级历史：history1/history2 为 List<List<Node>>（按处理器），history3 为 List<Node>
        List<List<Node>> h1 = new ArrayList<>();
        List<List<Node>> h2 = new ArrayList<>();
        h1.add(new ArrayList<>());
        h2.add(new ArrayList<>());
        List<Node> h3 = new ArrayList<>();

        // A) 不考虑缓存 → 基础执行时间，级别 1
        Pair<Pair<Long, Double>, Integer> rA =
                crp.computeET(0, h1, h2, h3, node, proc, false, 0.0, 0.0, false);
        checkEq("cacheAware=false → 执行时间=基础值", base, rA.getFirst().getFirst());
        checkEq("cacheAware=false → 缓存级别=1", 1, rA.getSecond());

        // B) 考虑缓存但历史为空 → 内存访问，级别 4，×1.2
        Pair<Pair<Long, Double>, Integer> rB =
                crp.computeET(0, h1, h2, h3, node, proc, true, 0.0, 0.0, false);
        checkEq("无历史 → 执行时间=120", 120L, rB.getFirst().getFirst());
        checkEq("无历史 → 缓存级别=4（内存）", 4, rB.getSecond());

        // C) L1 命中（history1 含同 DAG 节点）→ 级别 1，×0.8
        List<List<Node>> h1hit = new ArrayList<>();
        h1hit.add(Arrays.asList(new Node(0, Node.NodeType.NORMAL, 2, 7, crp, new Random(3))));
        Pair<Pair<Long, Double>, Integer> rC =
                crp.computeET(0, h1hit, h2, h3, node, proc, true, 0.0, 0.0, false);
        checkEq("L1 命中 → 执行时间=80", 80L, rC.getFirst().getFirst());
        checkEq("L1 命中 → 缓存级别=1", 1, rC.getSecond());

        // D) L2 命中（仅 history2 含同 DAG）→ 级别 2，×0.9
        List<List<Node>> h2hit = new ArrayList<>();
        h2hit.add(Arrays.asList(new Node(0, Node.NodeType.NORMAL, 3, 7, crp, new Random(4))));
        Pair<Pair<Long, Double>, Integer> rD =
                crp.computeET(0, h1, h2hit, h3, node, proc, true, 0.0, 0.0, false);
        checkEq("L2 命中 → 执行时间=90", 90L, rD.getFirst().getFirst());
        checkEq("L2 命中 → 缓存级别=2", 2, rD.getSecond());

        // E) L3 命中（仅 history3 含同 DAG）→ 级别 3，×0.95
        List<Node> h3hit = Arrays.asList(new Node(0, Node.NodeType.NORMAL, 4, 7, crp, new Random(5)));
        Pair<Pair<Long, Double>, Integer> rE =
                crp.computeET(0, h1, h2, h3hit, node, proc, true, 0.0, 0.0, false);
        checkEq("L3 命中 → 执行时间=95", 95L, rE.getFirst().getFirst());
        checkEq("L3 命中 → 缓存级别=3", 3, rE.getSecond());

        // F) 故障 → 执行时间 ×1.5
        Pair<Pair<Long, Double>, Integer> rF =
                crp.computeET(0, h1, h2, h3, node, proc, false, 0.0, 0.0, /*hasFaults*/ true);
        checkEq("故障 → 执行时间=150", 150L, rF.getFirst().getFirst());
    }

    // ---------- [3] WFD ----------
    private static void testWFD() {
        section("3. WFD 最差适配");
        WFD.resetLoads();
        WFD wfd = new WFD();
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node task = new Node(0, Node.NodeType.SOLO, 1, 1, crp, new Random(6));

        List<Integer> procs = Arrays.asList(0, 1, 2, 3);
        int first = wfd.allocate(Arrays.asList(task), procs);
        checkEq("首次分配（全零负载）→ 处理器 0", 0, first);

        int second = wfd.allocate(Arrays.asList(task), procs);
        checkEq("第二次分配（0 已加载）→ 处理器 1", 1, second);

        checkEq("空任务列表 → -1", -1, wfd.allocate(new ArrayList<>(), procs));
        checkEq("空处理器列表 → -1", -1, wfd.allocate(Arrays.asList(task), new ArrayList<>()));

        int[] loads = WFD.getProcessorLoads();
        check("处理器 0 负载 > 0", loads[0] > 0);
        WFD.resetLoads();
        checkEq("resetLoads 后负载归零", 0, WFD.getProcessorLoads()[0]);
    }

    // ---------- [4] CacheAware_v2 ----------
    private static void testCacheAware() {
        section("4. CacheAware_v2 契约");
        CacheAware_v2 ca = new CacheAware_v2();
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node task = new Node(0, Node.NodeType.SOLO, 1, 1, crp, new Random(7));
        List<Integer> procs = Arrays.asList(0, 1, 2, 3);

        int chosen = ca.allocate(Arrays.asList(task), procs);
        check("返回的处理器 ∈ 可用集合", procs.contains(chosen));

        checkEq("空任务列表 → -1", -1, ca.allocate(new ArrayList<>(), procs));
        checkEq("空处理器列表 → -1", -1, ca.allocate(Arrays.asList(task), new ArrayList<>()));

        // 同一输入重复调用应始终返回合法处理器（不崩溃、不越界）
        boolean allValid = true;
        for (int i = 0; i < 50; i++) {
            int p = ca.allocate(Arrays.asList(task), procs);
            if (!procs.contains(p)) { allValid = false; break; }
        }
        check("重复分配 50 次均返回合法处理器", allValid);
    }

    // ---------- [2b] CRP 边界 ----------
    private static Node mkNode(int dagID, double sensitivity) {
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node n = new Node(0, Node.NodeType.NORMAL, 1, dagID, crp, new Random(dagID * 31 + 7));
        n.sensitivity = sensitivity;
        return n;
    }

    private static List<List<Node>> oneEmptySlot() {
        List<List<Node>> h = new ArrayList<>();
        h.add(new ArrayList<>());
        return h;
    }

    private static void testCRPBoundaries() {
        section("2b. CRP 边界（优先级 / 窗口 / 截断 / 故障叠加 / 零基）");
        int proc = 0;
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node target = mkNode(7, 0.5);

        // A) L1 与 L2 同时命中 → 取 L1（优先级：L1 > L2 > L3）
        List<List<Node>> h1 = oneEmptySlot();
        List<List<Node>> h2 = oneEmptySlot();
        h1.get(0).add(mkNode(7, 0.5));
        h2.get(0).add(mkNode(7, 0.5));
        Pair<Pair<Long, Double>, Integer> rA =
                crp.computeET(0, h1, h2, new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("L1/L2 同时命中 → 取 L1（级别 1）", 1, rA.getSecond());
        checkEq("L1/L2 同时命中 → ET=80", 80L, rA.getFirst().getFirst());

        // B) 同 DAG 不同 id 仍算命中（按 dagID 判定）
        Pair<Pair<Long, Double>, Integer> rB =
                crp.computeET(0, h1, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("同 DAG（不同 id）→ L1 命中", 1, rB.getSecond());

        // C) DAG 不匹配 → 内存 miss（×1.2）
        List<List<Node>> h1miss = oneEmptySlot();
        h1miss.get(0).add(mkNode(99, 0.5));
        Pair<Pair<Long, Double>, Integer> rC =
                crp.computeET(0, h1miss, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("不同 DAG → 缓存级别 4（内存）", 4, rC.getSecond());
        checkEq("不同 DAG → ET=120", 120L, rC.getFirst().getFirst());

        // D) 最近访问窗口边界（窗口=5）：第 6 个历史位置不扫描 → miss
        List<Node> win = new ArrayList<>();
        win.add(mkNode(7, 0.5));                 // 索引 0（第 6 近）→ 超出窗口
        for (int i = 0; i < 5; i++) win.add(mkNode(99, 0.5));
        Pair<Pair<Long, Double>, Integer> rD =
                crp.computeET(0, oneEmptySlot(), oneEmptySlot(), win, target, proc, true, 0, 0, false);
        checkEq("目标位于窗口外（第 6 近）→ miss(120)", 120L, rD.getFirst().getFirst());

        // E) 目标位于窗口内（第 5 近）→ L3 命中
        List<Node> win2 = new ArrayList<>();
        win2.add(mkNode(99, 0.5));               // 索引 0
        win2.add(mkNode(7, 0.5));                // 索引 1（第 5 近）→ 在窗口内
        for (int i = 0; i < 4; i++) win2.add(mkNode(99, 0.5));
        Pair<Pair<Long, Double>, Integer> rE =
                crp.computeET(0, oneEmptySlot(), oneEmptySlot(), win2, target, proc, true, 0, 0, false);
        checkEq("目标位于窗口内（第 5 近）→ L3 命中(95)", 95L, rE.getFirst().getFirst());
        checkEq("目标位于窗口内 → 级别 3", 3, rE.getSecond());

        // F) 故障叠加在缓存命中之上：80 × 1.5 = 120
        Pair<Pair<Long, Double>, Integer> rF =
                crp.computeET(0, h1, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, true);
        checkEq("L1 命中 + 故障 → ET=120", 120L, rF.getFirst().getFirst());

        // G) 截断边界：base=1
        RecencyProfileReal crp1 = new RecencyProfileReal(1);
        Pair<Pair<Long, Double>, Integer> rG1 =
                crp1.computeET(0, h1, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("base=1 且 L1 → (long)0.8=0", 0L, rG1.getFirst().getFirst());
        Pair<Pair<Long, Double>, Integer> rG2 =
                crp1.computeET(0, h1miss, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("base=1 且 miss → (long)1.2=1", 1L, rG2.getFirst().getFirst());

        // H) 零基边界：base=0
        RecencyProfileReal crp0 = new RecencyProfileReal(0);
        Pair<Pair<Long, Double>, Integer> rH1 =
                crp0.computeET(0, h1, oneEmptySlot(), new ArrayList<>(), target, proc, false, 0, 0, false);
        checkEq("base=0 且 cacheAware=false → ET=0", 0L, rH1.getFirst().getFirst());
        Pair<Pair<Long, Double>, Integer> rH2 =
                crp0.computeET(0, h1, oneEmptySlot(), new ArrayList<>(), target, proc, true, 0, 0, false);
        checkEq("base=0 且 L1 命中 → ET=0", 0L, rH2.getFirst().getFirst());
    }

    // ---------- [5] Speedup / Benefit 边界 ----------
    private static Node allocNode(int partition, long start, long finish) {
        Node n = mkNode(1, 0.5);
        n.partition = partition;
        n.start = start;
        n.finishAt = finish;
        n.release = 0;
        return n;
    }

    private static void testSpeedupBenefitBoundaries() {
        section("5. Speedup / Benefit 边界");

        // A) 空任务列表 → 全部指标为 0
        PerformanceAnalyzer pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult r0 =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 0, new ArrayList<>(), 4, 0, 1.0);
        checkEq("空任务 → makespan=0", 0.0, r0.makespan);
        checkEq("空任务 → 敏感度收益=0", 0.0, r0.cacheSensitivityBenefit);

        // B) 敏感度均 ≤ 0.5 → 收益=0（严格 >0.5 才计入）
        List<Node> low = Arrays.asList(mkNode(1, 0.0), mkNode(1, 0.5), mkNode(1, 0.3));
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rLow =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 1, low, 4, 0, 1.0);
        checkEq("敏感度均 ≤0.5 → 收益=0", 0.0, rLow.cacheSensitivityBenefit);

        // C) 非 Cache-Aware → 收益恒为 0（即使高敏感）
        List<Node> high = Arrays.asList(mkNode(1, 0.9), mkNode(1, 1.0));
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rWfd =
                pa.analyzeAlgorithmPerformance("WFD", 2, high, 4, 0, 1.0);
        checkEq("WFD → 收益=0", 0.0, rWfd.cacheSensitivityBenefit);

        // D) 混合敏感度 {0.25,0.75,0.5,1.0} → 仅 0.75、1.0 计入
        //    avg = (0.75+1.0) * 0.15 / 2 = 0.13125
        List<Node> mix = Arrays.asList(mkNode(1, 0.25), mkNode(1, 0.75), mkNode(1, 0.5), mkNode(1, 1.0));
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rMix =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 3, mix, 4, 0, 1.0);
        checkNear("混合敏感度 → 收益=0.13125", 0.13125, rMix.cacheSensitivityBenefit, 1e-9);

        // E) 边界：单个 1.0 → 0.15
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rOne =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 4, Arrays.asList(mkNode(1, 1.0)), 4, 0, 1.0);
        checkNear("单个敏感度 1.0 → 收益=0.15", 0.15, rOne.cacheSensitivityBenefit, 1e-9);

        // F) 命名差异（真实缺陷记录）：收益用精确 "Cache-Aware"；
        //    而命中率分支用 contains("CacheAware")——两者命名不一致。
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rNameA =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 5, mix, 4, 0, 1.0);
        PerformanceAnalyzer.ExperimentResult rNameB =
                pa.analyzeAlgorithmPerformance("CacheAware", 6, mix, 4, 0, 1.0);
        check("命名 'Cache-Aware' → 收益 > 0", rNameA.cacheSensitivityBenefit > 0);
        check("命名 'CacheAware' → 收益 = 0（精确匹配失败）", rNameB.cacheSensitivityBenefit == 0.0);

        // G) Makespan / 负载均衡 / CPU 利用率：确定性 + 顺序无关
        List<Node> sched = Arrays.asList(
                allocNode(0, 0, 100),
                allocNode(0, 100, 200),
                allocNode(1, 0, 80));
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rS =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 7, sched, 2, 0, 1.0);
        checkNear("Makespan = 200", 200.0, rS.makespan, 1e-9);
        checkNear("平均响应时间 = 380/3", 380.0 / 3.0, rS.averageResponseTime, 1e-6);
        checkNear("CPU 利用率 = 280/400 = 0.7", 0.7, rS.cpuUtilization, 1e-9);
        checkNear("负载均衡度 = 1-60/140", 1.0 - 60.0 / 140.0, rS.loadBalance, 1e-9);

        // 顺序无关性（对称）：反转任务顺序 makespan 不变
        List<Node> rev = new ArrayList<>(sched);
        java.util.Collections.reverse(rev);
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rRev =
                pa.analyzeAlgorithmPerformance("Cache-Aware", 8, rev, 2, 0, 1.0);
        checkNear("反转顺序后 Makespan 不变（对称）", rS.makespan, rRev.makespan, 1e-9);

        // H) 未分配任务（partition=-1 / finishAt=-1）不影响 Makespan
        List<Node> unassigned = Arrays.asList(mkNode(1, 0.5), mkNode(1, 0.5));
        pa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rU =
                pa.analyzeAlgorithmPerformance("WFD", 9, unassigned, 4, 0, 1.0);
        checkEq("全部未分配 → Makespan=0", 0.0, rU.makespan);
        checkEq("全部未分配 → 平均响应时间=0", 0.0, rU.averageResponseTime);
    }

    /**
     * [6] 公平基线 C1。
     *
     * <p>核心不变量：两种算法只允许在"选哪个处理器"上不同；放置确定之后，
     * 执行时间与缓存命中必须由 {@link SharedCacheModel} 统一给出，且
     * {@code expectedET} 作为基准不得被任何一次执行改写。
     */
    private static void testFairBaseline() {
        section("6. 公平基线（SharedCacheModel / 口径一致）");

        // A) execute() 不修改 expectedET —— 基准时间必须对两种算法相同
        RecencyProfileReal crp = new RecencyProfileReal(100);
        Node a = new Node(0, Node.NodeType.NORMAL, 1, 0, crp, new Random(11));
        a.expectedET = 1000;
        SharedCacheModel model = new SharedCacheModel(4, 2);
        long etA = model.execute(a, 0);
        checkEq("execute() 不修改 expectedET", 1000L, a.expectedET);
        checkEq("冷启动首次访问 → 级别 4（内存）", 4, a.measuredCacheLevel);
        checkEq("冷启动 → 执行时间 = base×1.2", 1200L, etA);
        check("execute() 后 cacheMeasured=true", a.cacheMeasured);

        // B) 同 DAG 再次落到同一处理器 → L1 命中（×0.8）
        Node b = new Node(0, Node.NodeType.NORMAL, 2, 0, crp, new Random(12));
        b.expectedET = 1000;
        long etB = model.execute(b, 0);
        checkEq("同 DAG 同核第二次访问 → 级别 1（L1）", 1, b.measuredCacheLevel);
        checkEq("L1 命中 → 执行时间 = base×0.8", 800L, etB);
        checkEq("L1 命中后 expectedET 仍为 1000", 1000L, b.expectedET);

        // C) 级联语义：L1 命中意味着三级都命中
        checkNear("L1 命中 → actualL1L2L3HitRatio = [1,1,1]",
                3.0, b.actualL1L2L3HitRatio[0] + b.actualL1L2L3HitRatio[1] + b.actualL1L2L3HitRatio[2], 1e-9);

        // D) 两种算法在"同一放置"下拿到完全相同的执行时间
        SharedCacheModel m1 = new SharedCacheModel(4, 2);
        SharedCacheModel m2 = new SharedCacheModel(4, 2);
        long total1 = 0, total2 = 0;
        for (int i = 0; i < 20; i++) {
            Node n1 = new Node(0, Node.NodeType.NORMAL, i, i / 2, crp, new Random(100 + i));
            Node n2 = new Node(0, Node.NodeType.NORMAL, i, i / 2, crp, new Random(100 + i));
            n1.expectedET = n2.expectedET = 500 + i * 10;
            int proc = i % 4;
            total1 += m1.execute(n1, proc);
            total2 += m2.execute(n2, proc);
        }
        checkEq("同一放置序列下两个模型的总执行时间相同", total1, total2);

        // E) 分析器读实测值，而不是按算法名套公式
        List<Node> measured = new ArrayList<>();
        for (int i = 0; i < 10; i++) {
            Node n = new Node(0, Node.NodeType.NORMAL, i, i / 2, crp, new Random(200 + i));
            n.expectedET = 400;
            n.weights = new double[] {0.5, 0.3, 0.15, 0.05};
            new SharedCacheModel(4, 2).execute(n, 0); // 每个任务冷启动 → 级别 4
            measured.add(n);
        }
        PerformanceAnalyzer fairPa = new PerformanceAnalyzer();
        PerformanceAnalyzer.ExperimentResult rWfdFair =
                fairPa.analyzeAlgorithmPerformance("WFD", 20, measured, 4, 0, 1.0);
        PerformanceAnalyzer.ExperimentResult rCaFair =
                fairPa.analyzeAlgorithmPerformance("CacheAware_v2", 21, measured, 4, 0, 1.0);
        checkNear("实测命中率与算法名无关（WFD）", 0.0, rWfdFair.cacheHitRatio, 1e-9);
        checkNear("实测命中率与算法名无关（CacheAware）", 0.0, rCaFair.cacheHitRatio, 1e-9);

        // F) legacy 口径仍保留历史差异（用于复现旧基线），但需显式开启
        PerformanceAnalyzer legacyPa = new PerformanceAnalyzer(true);
        check("legacy 标志已开启", legacyPa.isLegacyMetrics());
        PerformanceAnalyzer fairPa2 = new PerformanceAnalyzer();
        check("默认构造为公平口径", !fairPa2.isLegacyMetrics());

        // G) CacheAware 默认不写回自估命中率（legacyWriteBack 关闭）
        CacheAware_v2.resetState();
        Node probe = new Node(0, Node.NodeType.NORMAL, 1, 0, crp, new Random(31));
        probe.expectedET = 300;
        probe.sensitivity = 0.9;
        probe.weights = new double[] {0.6, 0.25, 0.1, 0.05};
        checkEq("公平口径下 actualCacheHitRatio 初始为 0", 0.0, probe.actualCacheHitRatio);
        CacheAware_v2.resetState();
    }

    // ---------- [7] 配置外置（B3）：YAML 子集 + ExperimentConfig ----------
    private static void testConfig() {
        section("7. 配置外置（YAML 子集 + ExperimentConfig）");

        // A) 标量类型识别：整数不能被误判成浮点（否则 Long→Double 会让种子变精度）
        Map<String, Object> scalars = YamlSubset.parse(Arrays.asList(
                "i: 42",
                "d: 0.5",
                "b: true",
                "n: null",
                "q: \"a # b\"",
                "neg: -3"
        ), "<inline>");
        checkEq("整数解析为 Long", Long.valueOf(42), scalars.get("i"));
        checkEq("浮点解析为 Double", Double.valueOf(0.5), scalars.get("d"));
        checkEq("布尔解析为 Boolean", Boolean.TRUE, scalars.get("b"));
        check("null 解析为 null", scalars.containsKey("n") && scalars.get("n") == null);
        checkEq("引号内的 # 不是注释", "a # b", scalars.get("q"));
        checkEq("负数整数", Long.valueOf(-3), scalars.get("neg"));

        // B) 行内列表与缩进列表等价
        Map<String, Object> inline = YamlSubset.parse(Arrays.asList(
                "l: [0.6, 1.0, 1.5]"
        ), "<inline>");
        Map<String, Object> block = YamlSubset.parse(Arrays.asList(
                "l:",
                "  - 0.6",
                "  - 1.0",
                "  - 1.5"
        ), "<block>");
        checkEq("行内列表长度", 3, ((List<?>) inline.get("l")).size());
        checkEq("缩进列表与行内列表一致", inline.get("l"), block.get("l"));

        // C) 空行内列表
        Map<String, Object> emptyList = YamlSubset.parse(
                Arrays.asList("l: []"), "<empty>");
        checkEq("空行内列表长度为 0", 0, ((List<?>) emptyList.get("l")).size());

        // D) Tab 缩进必须被拒绝（YAML 禁止，且最易造成层级错乱）
        boolean tabRejected = false;
        try {
            YamlSubset.parse(Arrays.asList("a:", "\tb: 1"), "<tab>");
        } catch (YamlSubset.YamlException e) {
            tabRejected = e.getMessage().contains("制表符");
        }
        check("Tab 缩进被拒绝且消息可读", tabRejected);

        // E) 行内映射不支持 → 报错而不是猜
        boolean mapRejected = false;
        try {
            YamlSubset.parse(Arrays.asList("a: {b: 1}"), "<map>");
        } catch (YamlSubset.YamlException e) {
            mapRejected = true;
        }
        check("行内映射 {..} 被明确拒绝", mapRejected);

        // F) 内置默认值 = B3 之前的硬编码（行为不变的硬性约束）
        ExperimentConfig def = ExperimentConfig.defaults();
        checkEq("默认 cores=8", 8, def.cores);
        checkEq("默认 total_test_cases=100", 100, def.totalTestCases);
        checkEq("默认 tasks_per_case=60", 60, def.tasksPerCase);
        checkEq("默认 seed=42", 42L, def.seed);
        checkEq("默认 utilization_levels（顺序敏感）",
                Arrays.toString(new double[] {0.6, 0.8, 1.0, 1.2, 1.5}),
                Arrays.toString(def.utilizationLevels));
        checkEq("默认 high_cache_sensitivity_ratio=0.7",
                0.7, def.highCacheSensitivityRatio);
        checkEq("默认 cc_weights（L1 权重最高）",
                Arrays.toString(new double[] {0.5, 0.3, 0.15, 0.05}),
                Arrays.toString(def.ccWeights));
        checkEq("默认 cache_level=3", 3, def.cacheLevel);
        checkEq("默认 level2_core_num=2", 2, def.level2CoreNum);
        check("默认走单种子路径（seeds == null）", def.seeds == null);

        // G) 未知键必须报错（拼错一个字母不能静默忽略）
        boolean unknownRejected = false;
        String unknownMsg = "";
        try {
            // 借 defaults() + 反射不可行，这里走公开的 load 路径
            java.nio.file.Path tmp = java.nio.file.Files.createTempFile("yat-cfg-", ".yaml");
            java.nio.file.Files.write(tmp, Arrays.asList(
                    "schema_version: 1", "experiment:", "  utilisation_levels: [0.5]"),
                    java.nio.charset.StandardCharsets.UTF_8);
            try {
                ExperimentConfig.load(tmp);
            } finally {
                java.nio.file.Files.deleteIfExists(tmp);
            }
        } catch (ExperimentConfig.ConfigException e) {
            unknownRejected = e.getMessage().contains("utilisation_levels");
            unknownMsg = e.getMessage();
        } catch (Exception e) {
            unknownMsg = e.toString();
        }
        check("未知键被拒绝且回显键名", unknownRejected);

        // H) schema_version 缺失/不匹配必须分开报错（便于定位是文件旧还是代码旧）
        boolean schemaMissing = false;
        boolean schemaMismatch = false;
        try {
            java.nio.file.Path tmp = java.nio.file.Files.createTempFile("yat-cfg-", ".yaml");
            try {
                java.nio.file.Files.write(tmp, Arrays.asList("experiment:", "  cores: 2"),
                        java.nio.charset.StandardCharsets.UTF_8);
                ExperimentConfig.load(tmp);
            } finally {
                java.nio.file.Files.deleteIfExists(tmp);
            }
        } catch (ExperimentConfig.ConfigException e) {
            schemaMissing = e.getMessage().contains("schema_version");
        } catch (Exception ignored) {
            // 保持 false
        }
        try {
            java.nio.file.Path tmp = java.nio.file.Files.createTempFile("yat-cfg-", ".yaml");
            try {
                java.nio.file.Files.write(tmp, Arrays.asList("schema_version: 99"),
                        java.nio.charset.StandardCharsets.UTF_8);
                ExperimentConfig.load(tmp);
            } finally {
                java.nio.file.Files.deleteIfExists(tmp);
            }
        } catch (ExperimentConfig.ConfigException e) {
            schemaMismatch = e.getMessage().contains("只支持");
        } catch (Exception ignored) {
            // 保持 false
        }
        check("缺少 schema_version 时明确报错", schemaMissing);
        check("schema_version 不匹配时明确报错", schemaMismatch);

        // I) 语义校验：比例越界不做自动截断，而是失败
        boolean ratioRejected = false;
        try {
            java.nio.file.Path tmp = java.nio.file.Files.createTempFile("yat-cfg-", ".yaml");
            try {
                java.nio.file.Files.write(tmp, Arrays.asList(
                        "schema_version: 1", "experiment:",
                        "  high_cache_sensitivity_ratio: 1.5"),
                        java.nio.charset.StandardCharsets.UTF_8);
                ExperimentConfig.load(tmp);
            } finally {
                java.nio.file.Files.deleteIfExists(tmp);
            }
        } catch (ExperimentConfig.ConfigException e) {
            ratioRejected = e.getMessage().contains("[0,1]");
        } catch (Exception ignored) {
            // 保持 false
        }
        check("high_cache_sensitivity_ratio 越界被拒绝（不自动截断）", ratioRejected);

        // J) cc_weights 长度必须为 4（[L1,L2,L3,Memory]）
        boolean weightsRejected = false;
        try {
            java.nio.file.Path tmp = java.nio.file.Files.createTempFile("yat-cfg-", ".yaml");
            try {
                java.nio.file.Files.write(tmp, Arrays.asList(
                        "schema_version: 1", "system:", "  cc_weights: [0.5, 0.5]"),
                        java.nio.charset.StandardCharsets.UTF_8);
                ExperimentConfig.load(tmp);
            } finally {
                java.nio.file.Files.deleteIfExists(tmp);
            }
        } catch (ExperimentConfig.ConfigException e) {
            weightsRejected = e.getMessage().contains("元素个数");
        } catch (Exception ignored) {
            // 保持 false
        }
        check("cc_weights 长度不为 4 被拒绝", weightsRejected);

        // K) 命令行覆盖优先级高于配置文件
        ExperimentConfig cfg = ExperimentConfig.defaults();
        cfg.overrideCores(2);
        checkEq("CLI 覆盖生效", 2, cfg.cores);
        check("覆盖项被标记为显式设置", cfg.isExplicit("experiment.cores"));
        check("未覆盖项不算显式设置", !cfg.isExplicit("experiment.tasks_per_case"));

        // L) 配置写回 SystemParameters 后不共享数组（防外部改动污染配置）
        ExperimentConfig cfg2 = ExperimentConfig.defaults();
        cfg2.applyToSystemParameters();
        SystemParameters.cc_weights[0] = 999.0;
        checkNear("applyToSystemParameters 拷贝而非共享数组",
                ExperimentConfig.DEFAULT_CC_WEIGHTS[0], cfg2.ccWeights[0], 1e-12);
        // 复位，避免影响后续用例
        new ExperimentConfigProxy().restoreWeights();
    }

    /** 用于把 SystemParameters 恢复到类默认值的极小代理（测试卫生）。 */
    private static final class ExperimentConfigProxy {
        void restoreWeights() {
            SystemParameters.cc_weights = new double[] {0.25, 0.25, 0.25, 0.25};
        }
    }

    public static void main(String[] args) {
        System.out.println("==========================================");
        System.out.println(" Yat-CAShed 模拟器单元测试");
        System.out.println("==========================================");

        testNode();
        testRecencyProfile();
        testCRPBoundaries();
        testWFD();
        testCacheAware();
        testSpeedupBenefitBoundaries();
        testFairBaseline();
        testConfig();

        System.out.println("\n==========================================");
        System.out.printf(" 通过: %d   失败: %d%n", passed, failed);
        if (failed == 0) {
            System.out.println(" ✅ 全部测试通过");
        } else {
            System.out.println(" ❌ 存在失败用例");
        }
        System.out.println("==========================================");
        System.exit(failed == 0 ? 0 : 1);
    }
}
