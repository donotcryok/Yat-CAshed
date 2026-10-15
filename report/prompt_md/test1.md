# Yat_Casched 决赛版：基于“最大加速时间效益”的调度逻辑

## 核心前提

1.  **CRP模型**: 存在一个函数 `get_crp_ratio(recency)`，输入是任务上次运行距今的时间（近期性），输出是WCET的加速比（0.0 - 1.0）。
2.  **WCET**: 存在一个函数 `get_wcet(p)`，可以获取任务`p`的最坏情况执行时间。
3.  **加速时间效益 (Speedup Benefit)**: 我们定义 `Speedup_Benefit = get_crp_ratio(recency) * get_wcet(p)`。这个值代表了将任务`p`调度到某个CPU上预期可以节省的绝对时间。

---

## 1. 数据结构增强

### 1.1 `struct sched_yat_casched_entity` (每个任务的调度实体)
需要记录任务在**所有CPU**上的状态，而不仅仅是`last_cpu`。

```c
struct sched_yat_casched_entity {
    /* 
     * per_cpu_info: 数组，索引是CPU号。
     * 记录该任务在每个CPU上的“近期性”时间戳。
     */
    u64 per_cpu_recency[NR_CPUS]; 
    
    /* 任务的WCET，可以在任务创建或更新时设置 */
    u64 wcet; 
};
```

### 1.2 加速表 (Accelerator Table)
这不再是每个CPU一个，而是一个**全局的数据结构**，因为它存储的是“任务-CPU-效益”的三元组信息。一个简单的实现可以是全局的优先队列或红黑树，按`Speedup_Benefit`排序。

```c
/* 全局加速表，可以用更高效的数据结构如红黑树实现 */
struct list_head accelerator_table; 

struct accelerator_entry {
    struct list_head list;
    struct task_struct *p;
    int cpu;
    u64 benefit;
};
```
**注意**: 全局数据结构需要锁来保护并发访问。

---

## 2. 核心调度逻辑修改

### 2.1 `update_accelerator_table` (核心函数)
这个函数在每次调度事件（任务唤醒、睡眠、迁移）后被调用，负责更新全局加速表。

```c
void update_accelerator_table(void)
{
    struct task_struct *p;
    int i;

    /* 1. 清空旧的加速表 */
    // ... (加锁，清空list)

    /* 2. 遍历所有可运行的Yat_Casched任务 */
    for_each_process(p) {
        if (p->sched_class != &yat_casched_sched_class)
            continue;

        /* 3. 遍历所有该任务允许的CPU */
        for_each_cpu(i, p->cpus_ptr) {
            u64 recency = ktime_get() - p->yat_casched.per_cpu_recency[i];
            u64 crp_ratio = get_crp_ratio(recency); // 返回值应为百分比*100
            u64 benefit = (crp_ratio * p->yat_casched.wcet) / 100;

            /* 4. 创建新的加速表条目并插入 */
            if (benefit > 0) {
                // ... (创建entry，计算benefit，插入到全局加速表中)
            }
        }
    }
    // ... (解锁)
}
```

### 2.2 `select_task_rq_yat_casched` (为任务选择CPU)
当任务`p`唤醒时，直接查询加速表，找到对它来说`benefit`最高的CPU。

```c
int select_task_rq_yat_casched(struct task_struct *p, int prev_cpu, int wake_flags)
{
    /* 1. 更新加速表（或确保其为最新） */
    update_accelerator_table();

    /* 2. 在加速表中查找任务p的最佳CPU */
    int best_cpu = -1;
    u64 max_benefit = 0;
    struct accelerator_entry *entry;

    // ... (加锁)
    list_for_each_entry(entry, &accelerator_table, list) {
        if (entry->p == p && entry->benefit > max_benefit) {
            max_benefit = entry->benefit;
            best_cpu = entry->cpu;
        }
    }
    // ... (解锁)

    /* 3. 如果没找到（例如新任务），则使用负载均衡策略 */
    if (best_cpu == -1) {
        // ... (退化为初赛的负载最轻策略)
    }

    return best_cpu;
}
```

### 2.3 `pick_next_task_yat_casched` (选择下一个任务)
当CPU `i`空闲时，查询加速表，找到所有指向CPU `i`的条目中，`benefit`最高的那个任务。

```c
struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    /* 1. 确保加速表为最新 */
    // update_accelerator_table(); // 注意：频繁更新可能开销大，可优化

    /* 2. 在加速表中查找本CPU的最佳任务 */
    struct task_struct *best_task = NULL;
    u64 max_benefit = 0;
    struct accelerator_entry *entry;

    // ... (加锁)
    list_for_each_entry(entry, &accelerator_table, list) {
        if (entry->cpu == rq->cpu && entry->benefit > max_benefit) {
            /* 确保任务确实在运行队列中 */
            if (task_on_rq_queued(entry->p)) {
                 max_benefit = entry->benefit;
                 best_task = entry->p;
            }
        }
    }
    // ... (解锁)
    
    return best_task;
}
```

### 2.4 `switched_to_yat_casched` 和 `switched_from_yat_casched`
在任务切换时，需要更新`per_cpu_recency`时间戳。
- 当任务`p`在CPU `i`上被换下时，记录`p->yat_casched.per_cpu_recency[i] = ktime_get();`

---

## 3. 总结与挑战

这个方案完全贴合了论文的核心思想，将调度问题转化为了一个**最大化全局加速时间效益**的优化问题。

-   **优点**:
    -   **理论完备**: 决策依据清晰，可解释性强。
    -   **全局最优**: 每次决策都试图寻找全局最优解，而非局部最优。
-   **挑战**:
    -   **`update_accelerator_table`的开销**: 频繁地遍历所有任务和CPU来更新全局加速表，可能会引入巨大的调度延迟。**优化更新策略**是实现的关键（例如，只更新与事件相关的任务，而不是全局更新）。
    -   **并发控制**: 全局加速表需要精细的锁机制来保护，避免争用。
    -   **WCET的获取**: 在通用内核中准确获取每个任务的WCET是一个难题，在实现中可能需要用历史平均执行时间等作为近似值。

这个方案为你们的决赛实现提供了一个非常坚实且高标准的蓝图。