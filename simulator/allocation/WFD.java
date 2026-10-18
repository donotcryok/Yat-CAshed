package allocation;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import entity.Node;

/**
 * Worst Fit Decreasing (WFD) 任务分配算法
 * 
 * 经典负载均衡算法，采用最差适配策略，将任务分配给当前负载最轻的处理器。
 * 不考虑缓存局部性，适合作为对比基准。
 *
 * <p><b>公平性说明</b>：本类只做"放到哪个处理器"的决策，其决策依据是任务的
 * <em>基准</em>执行时间 {@code node.expectedET}。真正的执行时间与缓存命中由
 * {@code entity.SharedCacheModel} 统一计算，两种算法完全一致。
 *
 * <p><b>规模无关</b>：负载统计改为按处理器 ID 动态分配（早期实现硬编码
 * {@code int[8]}，在 {@code --cores>8} 时会静默退化，把负载当成 0 处理）。
 *
 * @author Cache-Aware Task Scheduling System
 */
public class WFD extends AllocationMethods {
    
    /** 处理器负载跟踪表：processorId -> 已分配基准负载 */
    private static final Map<Integer, Long> PROCESSOR_LOADS = new HashMap<>();
    
    /**
     * WFD任务分配核心算法
     * 
     * 选择当前负载最轻的处理器执行新任务
     */
    @Override
    public int allocate(List<Node> readyNodes, List<Integer> availableProc) {
        // 参数有效性检查
        if (readyNodes == null || readyNodes.isEmpty()
                || availableProc == null || availableProc.isEmpty()) {
            return -1;
        }
        
        // 获取要分配的任务（WFD每次处理一个任务）
        Node nodeToAllocate = readyNodes.get(0);
        long taskLoad = Math.max(0L, nodeToAllocate.expectedET); // 基准执行时间作为负载
        
        int bestProcessor = -1;      // 最优处理器ID
        long minLoad = Long.MAX_VALUE; // 当前最小负载
        
        // 遍历所有可用处理器，寻找负载最小的处理器
        for (int procId : availableProc) {
            long load = PROCESSOR_LOADS.getOrDefault(procId, 0L);
            if (load < minLoad) {
                minLoad = load;
                bestProcessor = procId;
            }
        }
        
        // 更新选中处理器的负载统计
        if (bestProcessor != -1) {
            PROCESSOR_LOADS.put(bestProcessor, minLoad + taskLoad);
        }
        
        // 返回选中的处理器ID，如果没有找到合适的处理器则返回第一个可用处理器
        return bestProcessor != -1 ? bestProcessor : availableProc.get(0);
    }
    
    /**
     * 重置所有处理器的负载统计
     */
    public static void resetLoads() {
        PROCESSOR_LOADS.clear();
    }
    
    /**
     * 获取当前所有处理器的负载情况。
     *
     * <p>返回长度 = 已登记的最大处理器 ID + 1（至少 8，保持与旧接口/测试的兼容性）。
     * 未出现的处理器负载为 0。
     */
    public static int[] getProcessorLoads() {
        int maxProc = -1;
        for (int procId : PROCESSOR_LOADS.keySet()) {
            if (procId > maxProc) {
                maxProc = procId;
            }
        }
        int len = Math.max(8, maxProc + 1);
        int[] loads = new int[len];
        for (Map.Entry<Integer, Long> e : PROCESSOR_LOADS.entrySet()) {
            int idx = e.getKey();
            if (idx < 0 || idx >= len) {
                continue;
            }
            long v = e.getValue();
            loads[idx] = (int) Math.min(Integer.MAX_VALUE, Math.max(0L, v));
        }
        return loads;
    }
}
