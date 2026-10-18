/*
 * Yat-CAShed - AJLR层级化DAG调度器 (Decision Final Version)
 *
 * 基于AJLR (Allocation of parallel Jobs based on Learned cache Recency)思想的
 * 层级化调度器实现：
 * 
 * 核心特性：
 * - DAG层：遵循全局固定优先级（gFPS），高优先级DAG的"就绪"作业优先被调度
 * - 作业层：利用"加速表"和"历史表"，做出最优的（作业，核心）分配决策
 * - 就绪作业优先队列统一管理所有待调度作业
 * - 二维加速表支持(job,cpu)→benefit查找
 * - 双向链表历史表记录时间戳→任务映射关系
 * - 采用AJLR的LCIF/MSF调度算法
 */
#include "sched.h"
#include <linux/sched/yat_casched.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/limits.h>
#include <linux/percpu.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

/*
 * tracepoint 实例化点。
 *
 * CREATE_TRACE_POINTS 必须恰好出现在**一个** .c 文件里，且要在
 * include 之前定义，否则 <trace/define_trace.h> 会为每个 TRACE_EVENT
 * 生成重复定义。这里就是 Yat_CASched 的唯一实例化点。
 */
#define CREATE_TRACE_POINTS
#include <trace/events/yat_casched.h>

/* 缓存热度阈值（jiffies）*/
#define YAT_CACHE_HOT_TIME	(HZ / 100)	/* 10ms */
#define YAT_MIN_GRANULARITY	(NSEC_PER_SEC / 1000)	/* 1ms - 更短的时间片 */
#define YAT_TIME_SLICE		(4 * NSEC_PER_MSEC)	/* 4ms 默认时间片 */

/*
 * 普通任务的默认 WCET。
 *
 * 用户态没有提供 WCET 的接口（SCHED_YAT_CASCHED 的 sched_priority 只有
 * 0..19），所以绑定到 task_struct 的隐式作业统一用 1ms：
 *   - 这是 yat_find_best_core_for_job() 里 benefit = (1000-crp)*wcet/1000
 *     的乘数，同一个值对**所有**任务都成立时，排序完全由 crp 决定 ——
 *     正是 AJLR 想要的「缓存热度优先」语义，不会被 WCET 差异干扰；
 *   - 若将来需要区分 WCET，应通过 sched_attr 扩展或 debugfs 注入，
 *     而不是在这里按 pid 猜。
 */
#define YAT_DEFAULT_WCET_NS	1000000ULL	/* 1ms */

/*
 * 两个连续的「全量选核」之间至少间隔这么久。
 *
 * 取值理由：yat_find_best_core_for_job() 的收益来自「任务换核后仍能命中
 * 上次留在该核缓存里的数据」。缓存的半衰期是数十微秒到毫秒量级，
 * 而重新决策的成本是 O(nr_cpus^2) 次历史表桶遍历。1ms 既远小于缓存
 * 完全失效的时间，又能把每任务每秒的决策次数压到 1000 次以内。
 * 在 64 核上这相当于把选核开销从「每次唤醒几万次桶访问」降到可忽略。
 */
#define YAT_DECISION_MIN_INTERVAL_NS	1000000ULL	/* 1ms */

/* 每个 CPU 历史表的软上限，超过后触发一次惰性裁剪，防止无限增长 */
#define YAT_HISTORY_MAX_RECORDS	512

/*
 * 单次 balance 回调最多分配多少个作业。
 * 纯粹是防御性上限：防止某条状态机 bug 让就绪池反复投喂同一作业时，
 * 调度器在软中断上下文里无限循环。正常负载下远达不到这个数。
 */
#define YAT_BALANCE_MAX_BATCH	256

/*
 * ============================================================================
 * 加锁规则（**修改本文件前必须先读这一段**）
 * ============================================================================
 *
 * 本调度类里一共有四把锁，加锁顺序（从外到内）必须严格遵守：
 *
 *   rq_lock(rq)                        -- 由核心调度器持有，本文件从不主动获取
 *     └─ global_dag_lock               -- 保护 global_dag_list / dag_id 分配
 *          └─ dag->lock                -- 保护 dag->jobs、total_jobs、completed_jobs
 *               └─ pool->lock          -- 保护 struct yat_ready_job_pool
 *     └─ global_history_tables[c].lock -- 保护该 CPU 的历史记录链表
 *     └─ table->lock                   -- 保护加速表哈希桶
 *
 * 约束：
 *
 * 1. 唯一的嵌套链是 global_dag_lock -> dag->lock -> pool->lock：
 *    - yat_update_ready_pool() 持 global_dag_lock + dag->lock，
 *      在内层调 yat_ready_pool_enqueue() 取 pool->lock；
 *    - yat_destroy_dag() 同样在 dag->lock 内调 yat_ready_pool_remove()。
 *    这条链的方向**不可反转** —— 任何先取 pool->lock 再取
 *    global_dag_lock/dag->lock 的写法都会构成 ABBA 死锁。
 *    其余锁（历史表、加速表）与任何锁都不嵌套：它们只在局部临界区里
 *    保护单一链表/哈希桶。任何新增的嵌套都会引入 ABBA 死锁风险，
 *    必须在此处登记。
 *
 * 2. **禁止在持锁期间睡眠式分配**。所有 kmalloc/kfree 都必须移到临界区之外：
 *    - 分配：持锁前用 GFP_ATOMIC/GFP_KERNEL 分配好，失败就放弃本次操作；
 *    - 释放：持锁期间只做 list_del/hlist_del，把节点挂到栈上的临时链表，
 *      解锁后再统一 kfree。kfree 可能进入回收器，持自旋锁调用是 UC 的。
 *
 * 3. `enqueue_task_yat_casched()/dequeue_task_yat_casched()` 的调用者
 *    （核心调度器）已经持有 rq_lock，`rq->yat_casched.tasks` 由 rq_lock
 *    保护。**不要**再引入 yat_pool_lock —— 它保护的 `yat_ready_job_pool`
 *    是另一份数据（作业就绪池），与 rq 的运行队列无关。
 *
 * 4. `balance_yat_casched()` 只在 CPU0 上执行全局分配，此时 rq_lock(CPU0)
 *    被持有。因此 `yat_schedule_ajlr()` 及其下游（yat_update_ready_pool、
 *    yat_find_best_core_for_job、yat_add_history_record）全部运行在
 *    「持 CPU0 rq_lock + 可能关中断」的上下文里：只能 GFP_ATOMIC、
 *    不能 mutex、不能 schedule()。
 *
 * 5. `for_each_online_cpu()` 下访问 `cpu_rq(cpu)->yat_casched` 只读字段
 *    （nr_running）是允许的；**不要**在未持该 CPU rq_lock 时写入其 rq。
 * ============================================================================
 */

/* 全局数据结构 */
static struct yat_ready_job_pool *global_ready_pool = NULL;     /* 就绪作业池 */
static struct yat_accel_table *global_accel_table = NULL;       /* 加速表 */
static struct yat_cpu_history_table global_history_tables[NR_CPUS]; /* 历史表 */
static struct list_head global_dag_list = LIST_HEAD_INIT(global_dag_list); /* 活动DAG列表 */
static DEFINE_SPINLOCK(global_dag_lock);                       /* DAG列表锁 */
static struct yat_dag_task *global_default_dag;                /* 传统任务的默认DAG */
static atomic_t next_dag_id = ATOMIC_INIT(1);                  /* DAG ID分配器 */
static atomic_t next_job_id = ATOMIC_INIT(1);                  /* 作业ID分配器 */

/*
 * 低开销统计计数器（A6）。
 *
 * 这些量每次调度都会更新，因此必须是 per-CPU 的普通加法（无锁、无原子
 * 操作）；读取时统一用 for_each_possible_cpu() 求和，允许读到略微不一致
 * 的快照 —— 统计量不需要线性一致。
 *
 * 与 tracepoint 的分工：
 *   - tracepoint 记录**事件序列**（谁在何时被分配到哪个核），用于离线分析；
 *   - 这里的计数器给出**运行中的总量**（经过多少次 balance、迁移多少次），
 *     用于 debugfs 一眼查看，不需要开 trace。
 */
struct yat_casched_stats {
	u64 balance_runs;	/* balance 回调真正拿到全局锁的次数 */
	u64 balance_skips;	/* 抢锁失败、本次跳过的次数（说明争用程度） */
	u64 jobs_placed;	/* AJLR 成功完成 (job, cpu) 分配的次数 */
	u64 jobs_nocore;	/* 就绪作业但找不到可用核心的次数 */
	u64 history_records;	/* 写入历史表的执行区间条数 */
	u64 accel_inserts;	/* 加速表成功插入条数 */
	u64 accel_dup;		/* 加速表重复条目（EEXIST）次数 */
};

static DEFINE_PER_CPU(struct yat_casched_stats, yat_stats);

/* debugfs 根节点：/sys/kernel/debug/yat_casched（未编译 DEBUG_FS 时为 NULL） */
static struct dentry *yat_debugfs_root;

/*
 * 计数器增量。
 *
 * 用 this_cpu_inc()/this_cpu_add() 而不是 atomic_t：
 *   - 这些计数器在热点路径上，atomic_t 会引入 lock 前缀，跨核争用严重；
 *   - 计数器只用于观测，求和时允许短暂不一致（最多丢掉一次正在进行的加法）。
 * 注意 this_cpu_* 隐含关抢占（单指令 RMW），因此在持有 rq_lock 的上下文里
 * 调用是安全的。
 */
#define YAT_STAT_INC(field)	this_cpu_inc(yat_stats.field)
#define YAT_STAT_ADD(field, v)	this_cpu_add(yat_stats.field, (v))

/*
 * 汇总所有 CPU 上的某个计数器。
 *
 * 用 offsetof + for_each_possible_cpu 而不是为每个字段写一遍循环：
 * 7 个计数器写 7 遍样板代码既易错又难维护，而 struct 布局是编译期常量，
 * 按偏移访问完全类型安全（字段都是 u64，对齐一致）。
 */
static u64 yat_stats_sum(size_t offset)
{
	u64 total = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		total += *(u64 *)((char *)per_cpu_ptr(&yat_stats, cpu) + offset);

	return total;
}

#define YAT_STAT_SUM(field)	\
	yat_stats_sum(offsetof(struct yat_casched_stats, field))

/*
 * debugfs: /sys/kernel/debug/yat_casched/stats
 *
 * 输出的是**累计量**，不是瞬时值。要观察时序请开 tracepoint；
 * 这里回答的是"调度器到底跑了多少轮、分配成功多少次、跳过多少次"。
 * 特别是 balance_skips / balance_runs 的比值，直接反映全局调度的争用程度 ——
 * 如果 skips 远大于 runs，说明 trylock 单飞设计下只有一颗核在干活，
 * 后续需要把 yat_balance_lock 换成更细粒度的分片锁。
 */
static int yat_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "balance_runs      %llu\n", YAT_STAT_SUM(balance_runs));
	seq_printf(m, "balance_skips     %llu\n", YAT_STAT_SUM(balance_skips));
	seq_printf(m, "jobs_placed       %llu\n", YAT_STAT_SUM(jobs_placed));
	seq_printf(m, "jobs_nocore       %llu\n", YAT_STAT_SUM(jobs_nocore));
	seq_printf(m, "history_records   %llu\n", YAT_STAT_SUM(history_records));
	seq_printf(m, "accel_inserts     %llu\n", YAT_STAT_SUM(accel_inserts));
	seq_printf(m, "accel_dup         %llu\n", YAT_STAT_SUM(accel_dup));
	seq_printf(m, "nr_cpu_ids        %u\n", nr_cpu_ids);
	return 0;
}

static int yat_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, yat_stats_show, NULL);
}

static const struct file_operations yat_stats_fops = {
	.open		= yat_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * debugfs: /sys/kernel/debug/yat_casched/ready_pool
 *
 * 就绪池当前快照。持 pool->lock 读取，所以看到的是某一瞬间的一致视图。
 * 每行一个作业，按池内顺序（即调度顺序）打印。
 *
 * 必须用 spin_lock_irqsave()，不能用 spin_lock()：
 *   pool->lock 在调度热路径里总是在**关中断**状态下被获取的
 *   （balance_yat_casched() 运行在 rq_lock 内，而 rq_lock 是 irq-safe 的），
 *   因此 pool->lock 在 lockdep 眼里是 HARDIRQ-safe 锁。debugfs 的 read()
 *   跑在普通进程上下文（中断是开的），若这里只 spin_lock() 而不关中断，
 *   则「进程持有 pool->lock」+「中断里 scheduler_tick() 取 rq->lock 再取
 *   pool->lock」就构成真实的 ABBA 死锁。实跑 lockdep 已报出
 *   "possible irq lock inversion dependency detected"。
 */
static int yat_ready_pool_show(struct seq_file *m, void *v)
{
	struct yat_ready_job_pool *pool = READ_ONCE(global_ready_pool);
	struct yat_job *job;
	unsigned long flags;

	if (!pool) {
		seq_puts(m, "ready pool 尚未初始化\n");
		return 0;
	}

	spin_lock_irqsave(&pool->lock, flags);
	seq_printf(m, "nr_jobs %d\n", pool->nr_jobs);
	seq_puts(m, "pid\tjob_id\tprio\tdag_prio\twcet\tstate\n");
	list_for_each_entry(job, &pool->head, ready_list) {
		seq_printf(m, "%d\t%d\t%d\t%d\t%llu\t%d\n",
			   job->task ? job->task->pid : 0,
			   job->job_id, job->job_priority,
			   job->dag ? job->dag->priority : 0,
			   (unsigned long long)job->wcet,
			   job->state);
	}
	spin_unlock_irqrestore(&pool->lock, flags);
	return 0;
}

static int yat_ready_pool_open(struct inode *inode, struct file *file)
{
	return single_open(file, yat_ready_pool_show, NULL);
}

static const struct file_operations yat_ready_pool_fops = {
	.open		= yat_ready_pool_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * debugfs: /sys/kernel/debug/yat_casched/dags
 *
 * 活动 DAG 与作业状态分布。用来看"作业卡在哪一层"：
 * waiting 数远大于 ready 通常意味着依赖图没被正确唤醒
 * （yat_job_complete() 传播失败），而不是核心不够。
 */
static int yat_dags_show(struct seq_file *m, void *v)
{
	struct yat_dag_task *dag;
	struct yat_job *job;
	unsigned long flags;

	seq_puts(m, "dag_id\tprio\ttotal\tcompleted\twaiting\tready\trunning\n");

	/* 同上：这两把锁在调度路径里都是关中断持有的，读取端必须一起关中断。 */
	spin_lock_irqsave(&global_dag_lock, flags);
	list_for_each_entry(dag, &global_dag_list, dag_list) {
		int waiting = 0, ready = 0, running = 0;

		/*
		 * 嵌套子类：本函数持有 global_dag_lock 再取 dag->lock，
		 * 而调度路径里也有同样的顺序（如 yat_update_ready_pool()），
		 * 用 _nested(1) 明确告诉 lockdep 这是预期的层次，
		 * 避免把合法的「父 → 子」关系误判为环。
		 */
		spin_lock_nested(&dag->lock, 1);
		list_for_each_entry(job, &dag->jobs, job_list) {
			switch (job->state) {
			case YAT_JOB_WAITING:	waiting++;	break;
			case YAT_JOB_READY:	ready++;	break;
			case YAT_JOB_RUNNING:	running++;	break;
			default:				break;
			}
		}
		seq_printf(m, "%d\t%d\t%d\t%d\t\t%d\t%d\t%d\n",
			   dag->dag_id, dag->priority, dag->total_jobs,
			   dag->completed_jobs, waiting, ready, running);
		spin_unlock(&dag->lock);
	}
	spin_unlock_irqrestore(&global_dag_lock, flags);
	return 0;
}

static int yat_dags_open(struct inode *inode, struct file *file)
{
	return single_open(file, yat_dags_show, NULL);
}

static const struct file_operations yat_dags_fops = {
	.open		= yat_dags_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * debugfs: /sys/kernel/debug/yat_casched/placements
 *
 * 「决策 vs 实际」对照表 —— assigned_cpu 这个字段存在的意义就在这里。
 *
 *   assigned : AJLR 认为该作业应该在哪颗核上运行（YAT_INVALID_CORE 表示
 *              尚未决策，通常是刚从 fork 出来、还没经历过一次唤醒）；
 *   actual   : 该作业当前实际被绑定的核（task_cpu()）；
 *   verdict  : ok      两者一致
 *              pending 尚未决策
 *              denied  不一致 —— 说明 is_cpu_allowed()/亲和性掩码否决了
 *                      AJLR 的决策，或任务在决策后又被 migrate 走了
 *
 * denied 占比是衡量「算法决策真正生效程度」的唯一直接指标。若该比例
 * 长期偏高，说明瓶颈不在 AJLR 本身，而在 cpuset/亲和性配置。
 *
 * 一致性说明：遍历期间持有 global_dag_lock 与 dag->lock，
 * 而 yat_destroy_job() 也要先取 dag->lock 才会摘链并 kfree，
 * 因此只要作业还挂在 dag->jobs 上，其内存就是有效的。
 */
static int yat_placements_show(struct seq_file *m, void *v)
{
	struct yat_dag_task *dag;
	struct yat_job *job;
	unsigned long flags;
	int ok = 0, pending = 0, denied = 0;

	seq_puts(m, "job_id\tpid\tprio\tassigned\tactual\tverdict\n");

	spin_lock_irqsave(&global_dag_lock, flags);
	list_for_each_entry(dag, &global_dag_list, dag_list) {
		spin_lock_nested(&dag->lock, 1);
		list_for_each_entry(job, &dag->jobs, job_list) {
			int assigned = READ_ONCE(job->assigned_cpu);
			int actual = job->task ? task_cpu(job->task) : -1;
			const char *verdict;

			if (assigned == YAT_INVALID_CORE) {
				verdict = "pending";
				pending++;
			} else if (assigned == actual) {
				verdict = "ok";
				ok++;
			} else {
				verdict = "denied";
				denied++;
			}

			seq_printf(m, "%d\t%d\t%d\t%d\t%d\t%s\n",
				   job->job_id,
				   job->task ? job->task->pid : -1,
				   job->job_priority, assigned, actual, verdict);
		}
		spin_unlock(&dag->lock);
	}
	spin_unlock_irqrestore(&global_dag_lock, flags);

	seq_printf(m, "\nsummary: ok=%d denied=%d pending=%d\n",
		   ok, denied, pending);
	return 0;
}

static int yat_placements_open(struct inode *inode, struct file *file)
{
	return single_open(file, yat_placements_show, NULL);
}

static const struct file_operations yat_placements_fops = {
	.open		= yat_placements_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * 创建 debugfs 节点。失败不影响调度功能，只是少了一个观测入口。
 *
 * 返回 int 而不是 void：initcall_t 的类型是 int (*)(void)，用
 * late_initcall() 注册的函数必须严格匹配，否则 ____define_initcall()
 * 里的 static_assert(__same_type(initcall_t, &fn)) 会直接编译失败。
 * 返回值恒为 0 —— 节点创建失败不需要阻止内核继续启动。
 */
static int __init yat_debugfs_init(void)
{
	yat_debugfs_root = debugfs_create_dir("yat_casched", NULL);
	if (!yat_debugfs_root)
		return 0;

	debugfs_create_file("stats", 0444, yat_debugfs_root, NULL,
			    &yat_stats_fops);
	debugfs_create_file("ready_pool", 0444, yat_debugfs_root, NULL,
			    &yat_ready_pool_fops);
	debugfs_create_file("dags", 0444, yat_debugfs_root, NULL,
			    &yat_dags_fops);
	debugfs_create_file("placements", 0444, yat_debugfs_root, NULL,
			    &yat_placements_fops);

	return 0;
}

/*
 * 必须用 late_initcall 而不是在 sched_init() 里创建节点：
 * sched_init() 跑在 start_kernel() 的早期，此时 debugfs 尚未挂载
 * （debugfs_init() 是 core_initcall，且 root 目录要到 vfs_caches_init()
 * 之后才可用），debugfs_create_dir() 会直接返回 -ENODEV 或 NULL。
 *
 * late_initcall 阶段用户态还没起，但内核初始化已完成，debugfs 可写。
 * 创建失败（CONFIG_DEBUG_FS=n 时 debugfs_create_dir() 返回 ERR_PTR 兼容值）
 * 不影响任何调度路径 —— 所有统计写入都是 this_cpu_inc()，与节点是否存在无关。
 */
late_initcall(yat_debugfs_init);

/*
 * AJLR核心数据结构管理函数
 */

/* 前置声明 */
static void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags);
static void yat_ajlr_dequeue_task(struct rq *rq, struct task_struct *p, int flags);
/*
 * yat_get_default_dag() 定义在文件末尾（与 yat_ajlr_enqueue_task 相邻，
 * 便于读者对照"默认 DAG 从哪来"）。而 select_task_rq() 的选核路径在
 * 文件中部就要用到它，因此这里补一个前置声明。
 */
static struct yat_dag_task *yat_get_default_dag(void);
struct yat_job *yat_create_job_for_task(struct task_struct *task, int job_priority, u64 wcet, struct yat_dag_task *dag);
void yat_complete_task_job(struct task_struct *task);
void yat_create_test_dag(void);

/* Ready Pool 函数声明 */
static int yat_ready_pool_init(struct yat_ready_job_pool *pool);
static void yat_ready_pool_destroy(struct yat_ready_job_pool *pool);
static int yat_ready_pool_enqueue(struct yat_ready_job_pool *pool, struct yat_job *job);
static struct yat_job *yat_ready_pool_dequeue(struct yat_ready_job_pool *pool);
static void yat_ready_pool_remove(struct yat_ready_job_pool *pool, struct yat_job *job);

/* History 函数声明 */
static void yat_init_history_tables(struct yat_cpu_history_table *tables);
static void yat_destroy_history_tables(struct yat_cpu_history_table *tables) __maybe_unused;
static int yat_add_history_record(int cpu, int job_id, u64 timestamp, u64 exec_time);
static u64 yat_get_task_last_timestamp(int cpu, int job_id);
static u64 yat_sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts);

/* 初始化就绪作业池 */
static int yat_ready_pool_init(struct yat_ready_job_pool *pool)
{
    if (!pool)
        return -EINVAL;
    
    INIT_LIST_HEAD(&pool->head);
    spin_lock_init(&pool->lock);
    pool->nr_jobs = 0;
    
    return 0;
}

/* 销毁就绪作业池（当前仅在模块级清理路径使用，保留以免随进程退出泄漏） */
static void __maybe_unused yat_ready_pool_destroy(struct yat_ready_job_pool *pool)
{
    struct yat_job *job, *tmp;
    
    if (!pool)
        return;
    
    spin_lock(&pool->lock);
    list_for_each_entry_safe(job, tmp, &pool->head, ready_list) {
        list_del(&job->ready_list);
    }
    pool->nr_jobs = 0;
    spin_unlock(&pool->lock);
}

/* 就绪作业池入队（按优先级排序） */
static int yat_ready_pool_enqueue(struct yat_ready_job_pool *pool, struct yat_job *job)
{
    struct yat_job *existing_job;
    struct list_head *pos;
    unsigned int nr_jobs;
    
    if (!pool || !job)
        return -EINVAL;
    
    spin_lock(&pool->lock);
    
    /* 按优先级排序插入：DAG优先级 > 作业优先级 > WCET */
    list_for_each(pos, &pool->head) {
        existing_job = list_entry(pos, struct yat_job, ready_list);
        
        /* 主键：DAG优先级 */
        if (job->dag->priority > existing_job->dag->priority)
            break;
        if (job->dag->priority < existing_job->dag->priority)
            continue;
            
        /* 次键：作业优先级 */
        if (job->job_priority > existing_job->job_priority)
            break;
        if (job->job_priority < existing_job->job_priority)
            continue;
            
        /* 三键：WCET（大的在前） */
        if (job->wcet > existing_job->wcet)
            break;
    }
    
    list_add_tail(&job->ready_list, pos);
    job->state = YAT_JOB_READY;
    pool->nr_jobs++;
    nr_jobs = pool->nr_jobs;
    
    spin_unlock(&pool->lock);

    /*
     * tracepoint 放在锁外：trace 事件内部要取 ring buffer 的每 CPU 锁，
     * 虽然不阻塞，但保持「临界区内只碰数据结构」的惯例能让 lockdep 的
     * 持有链更易读，也避免 ring buffer 锁与 pool->lock 形成嵌套。
     * 静态分支未开启时这里只有一条 nop，热路径代价可忽略。
     */
    trace_yat_casched_enqueue(job->task ? job->task->pid : 0,
                              job->job_id, job->job_priority, nr_jobs);
    return 0;
}

/* 就绪作业池出队（取队头） */
static struct yat_job *yat_ready_pool_dequeue(struct yat_ready_job_pool *pool)
{
    struct yat_job *job = NULL;
    unsigned int nr_jobs = 0;
    
    if (!pool)
        return NULL;
    
    spin_lock(&pool->lock);
    if (!list_empty(&pool->head)) {
        job = list_first_entry(&pool->head, struct yat_job, ready_list);
        list_del_init(&job->ready_list);
        job->state = YAT_JOB_RUNNING;
        pool->nr_jobs--;
        nr_jobs = pool->nr_jobs;
    }
    spin_unlock(&pool->lock);

    if (job)
        trace_yat_casched_dequeue(job->task ? job->task->pid : 0,
                                  job->job_id, nr_jobs);
    
    return job;
}

/*
 * 原 yat_ready_pool_peek() 已删除。
 *
 * "peek 之后再做 dequeue" 本身就是 TOCTOU：两者之间池头可能被换掉，
 * 于是 dequeue 弹出的作业和基于 peek 结果算出的 best_core 对不上
 * （详见 yat_schedule_ajlr() 的注释）。现在的调用点是原子 dequeue，
 * 没有任何地方需要非破坏性查看，留着只会诱使后来者重新引入该 bug。
 */

/* 从就绪池移除指定作业 */
static void yat_ready_pool_remove(struct yat_ready_job_pool *pool, struct yat_job *job)
{
    if (!pool || !job)
        return;
    
    spin_lock(&pool->lock);
    if (!list_empty(&job->ready_list)) {
        list_del_init(&job->ready_list);
        pool->nr_jobs--;
    }
    spin_unlock(&pool->lock);
}

/*
 * DAG和作业管理函数
 */

/*
 * 创建 DAG。
 *
 * gfp 必须由调用者显式给出：本函数既能从进程上下文调用（如
 * yat_create_test_dag()，可用 GFP_KERNEL），也可能从
 * yat_ajlr_enqueue_task() 这条「持 rq_lock、可能关中断」的原子路径
 * 首次触达（那里只能 GFP_ATOMIC）。原实现硬编码 GFP_KERNEL，
 * 在原子上下文里会触发 "sleeping function called from invalid context"。
 *
 * dag_id 分配用 atomic_inc_return()：原来 `next_dag_id++` 是
 * 无保护的 read-modify-write，多 CPU 同时创建 DAG 会拿到重复 ID，
 * 而 dag_id 是调试/统计路径里识别 DAG 的唯一键。
 */
static struct yat_dag_task *__yat_create_dag(int dag_id, int dag_priority, gfp_t gfp)
{
    struct yat_dag_task *dag;
    
    dag = kmalloc(sizeof(*dag), gfp);
    if (!dag)
        return NULL;
    
    dag->dag_id = (dag_id <= 0) ? atomic_inc_return(&next_dag_id) : dag_id;
    dag->priority = dag_priority;
    dag->total_jobs = 0;
    dag->completed_jobs = 0;
    INIT_LIST_HEAD(&dag->dag_list);
    INIT_LIST_HEAD(&dag->jobs);
    spin_lock_init(&dag->lock);
    
    spin_lock(&global_dag_lock);
    list_add_tail(&dag->dag_list, &global_dag_list);
    spin_unlock(&global_dag_lock);
    
    return dag;
}

/* 创建DAG（进程上下文默认入口） */
struct yat_dag_task *yat_create_dag(int dag_id, int dag_priority)
{
    return __yat_create_dag(dag_id, dag_priority, GFP_KERNEL);
}

/*
 * 销毁DAG。
 *
 * 两处必须小心：
 *
 * 1. 作业可能同时挂在两处：dag->jobs 与全局就绪池的 pool->head。
 *    原实现只摘 dag->jobs 就 kfree，若该作业仍在就绪池里，随后的
 *    yat_ready_pool_peek()/dequeue() 会对已释放内存做 list 操作 ——
 *    use-after-free。因此必须先把作业从就绪池摘除。
 *
 * 2. kfree 不能在自旋锁内调用（kfree 可能进入回收器并抢其他锁）。
 *    这里摘链与释放分离：临界区里只 list_del，解锁后统一 kfree。
 *    借用 job->ready_list 串一个栈上的临时链表来完成。
 */
void yat_destroy_dag(struct yat_dag_task *dag)
{
    struct yat_job *job, *tmp;
    LIST_HEAD(doomed);

    if (!dag)
        return;

    spin_lock(&global_dag_lock);
    list_del_init(&dag->dag_list);
    spin_unlock(&global_dag_lock);

    /*
     * 先从就绪池摘除：yat_ready_pool_remove() 只碰 job->ready_list 和
     * pool->lock，与 dag->lock 无关系，所以在 dag->lock 之外调用。
     */
    if (global_ready_pool) {
        spin_lock(&dag->lock);
        list_for_each_entry(job, &dag->jobs, job_list)
            yat_ready_pool_remove(global_ready_pool, job);
        spin_unlock(&dag->lock);
    }

    /* 摘 dag->jobs，挂到临时链表上，锁外释放 */
    spin_lock(&dag->lock);
    list_for_each_entry_safe(job, tmp, &dag->jobs, job_list) {
        list_del_init(&job->job_list);
        list_add_tail(&job->ready_list, &doomed);
    }
    spin_unlock(&dag->lock);

    list_for_each_entry_safe(job, tmp, &doomed, ready_list) {
        list_del_init(&job->ready_list);
        kfree(job);
    }

    kfree(dag);
}

/*
 * 创建作业。gfp 的约束同 __yat_create_dag()：可能从原子上下文到达。
 */
static struct yat_job *__yat_create_job(int job_id, int job_priority, u64 wcet,
                                       struct yat_dag_task *dag, gfp_t gfp)
{
    struct yat_job *job;
    
    if (!dag)
        return NULL;
    
    job = kmalloc(sizeof(*job), gfp);
    if (!job)
        return NULL;
    
    job->job_id = (job_id <= 0) ? atomic_inc_return(&next_job_id) : job_id;
    job->job_priority = job_priority;
    job->wcet = wcet;
    job->dag = dag;
    job->task = NULL;
    job->state = YAT_JOB_WAITING;
    job->pending_predecessors = 0;
    job->assigned_cpu = YAT_INVALID_CORE;
    job->last_decision_ns = 0;
    
    INIT_LIST_HEAD(&job->job_list);
    INIT_LIST_HEAD(&job->ready_list);
    INIT_LIST_HEAD(&job->predecessors);
    INIT_LIST_HEAD(&job->successors);
    
    spin_lock(&dag->lock);
    list_add_tail(&job->job_list, &dag->jobs);
    dag->total_jobs++;
    spin_unlock(&dag->lock);
    
    return job;
}

/* 创建作业（进程上下文默认入口） */
struct yat_job *yat_create_job(int job_id, int job_priority, u64 wcet, struct yat_dag_task *dag)
{
    return __yat_create_job(job_id, job_priority, wcet, dag, GFP_KERNEL);
}

/*
 * 销毁作业。
 *
 * 关键点：必须先把作业从**全局就绪池**摘下来，再摘 DAG 链表，最后才 kfree。
 * 原实现只做了 list_del(&job->job_list) 就 kfree，如果这个作业此刻正躺在
 * yat_ready_pool 里，kfree 之后 pool->head 仍指向已释放内存 —— 下一次
 * yat_ready_pool_dequeue() 就是 use-after-free（KASAN 必报）。
 *
 * 顺序也重要：先 pool 再 dag->lock，与
 * global_dag_lock -> dag->lock -> pool->lock 的既有方向不矛盾
 * （这里不持 global_dag_lock，只取 pool->lock，两把锁不嵌套）。
 */
void yat_destroy_job(struct yat_job *job)
{
    if (!job)
        return;

    /* 1. 从全局就绪池摘除（若在池中）。job->ready_list 为空时该函数是 no-op */
    if (global_ready_pool)
        yat_ready_pool_remove(global_ready_pool, job);

    /* 2. 从所属 DAG 的作业链表摘除 */
    if (job->dag) {
        spin_lock(&job->dag->lock);
        if (!list_empty(&job->job_list))
            list_del_init(&job->job_list);
        if (job->dag->total_jobs > 0)
            job->dag->total_jobs--;
        spin_unlock(&job->dag->lock);
    }

    kfree(job);
}

/*
 * 建立依赖关系：predecessor 完成之前，job 不可进入就绪池。
 *
 * 原实现只做 job->pending_predecessors++，既没有把 job 挂到 predecessor
 * 的后继链表，也没有把 predecessor 挂到 job 的前驱链表 —— 于是
 * yat_job_complete() 无从知道该唤醒谁，整条 DAG 依赖链永远停在
 * pending_predecessors>0 的状态，作业永远不会就绪。
 */
void yat_job_add_dependency(struct yat_job *job, struct yat_job *predecessor)
{
    if (!job || !predecessor || job == predecessor)
        return;

    /* 依赖边写入需要同时改两端，用 job 自己的 DAG 锁串行化 */
    if (job->dag)
        spin_lock(&job->dag->lock);
    else if (predecessor->dag)
        spin_lock(&predecessor->dag->lock);

    list_add_tail(&job->predecessors, &predecessor->successors);
    job->pending_predecessors++;

    if (job->dag)
        spin_unlock(&job->dag->lock);
    else if (predecessor->dag)
        spin_unlock(&predecessor->dag->lock);
}

/*
 * 作业完成处理。
 *
 * 语义：把自己标记完成，然后逐个通知后继 —— 每个后继的
 * pending_predecessors 减一，减到 0 的就可以被
 * yat_update_ready_pool() 收进就绪池。这不是"唤醒"，因为 AJLR 的就绪
 * 判定完全由 ready_pool + pending_predecessors 驱动，没有等待队列。
 */
void yat_job_complete(struct yat_job *job)
{
    struct yat_job *succ, *tmp;
    struct yat_dag_task *dag;
    LIST_HEAD(propagate);

    if (!job)
        return;

    dag = job->dag;

    /* 1. 在锁内完成状态迁移 + 把后继链表整段搬出来 */
    if (dag)
        spin_lock(&dag->lock);

    if (job->state == YAT_JOB_COMPLETED)
        goto out_unlock;   /* 幂等：重复完成不重复递减后继计数 */

    job->state = YAT_JOB_COMPLETED;
    if (dag && dag->completed_jobs < dag->total_jobs)
        dag->completed_jobs++;

    list_splice_init(&job->successors, &propagate);

    if (dag)
        spin_unlock(&dag->lock);

    /* 2. 锁外递减后继计数（后面还会取 dag->lock，避免自我嵌套） */
    list_for_each_entry_safe(succ, tmp, &propagate, predecessors) {
        list_del_init(&succ->predecessors);
        if (succ->pending_predecessors > 0)
            succ->pending_predecessors--;
    }
    return;

out_unlock:
    if (dag)
        spin_unlock(&dag->lock);
}

/*
 * 历史表管理函数 - 基于双向链表的新实现
 */

/* 初始化历史表 */
static void yat_init_history_tables(struct yat_cpu_history_table *tables)
{
    int cpu;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        INIT_LIST_HEAD(&tables[cpu].time_list);
        spin_lock_init(&tables[cpu].lock);
        tables[cpu].last_cleanup_time = jiffies;
        tables[cpu].nr_records = 0;
    }
}

/*
 * 摘掉整条历史记录链并返回，调用者必须在**锁外**调用 kfree。
 * 这是把「摘链」与「释放」分离的标准做法：kfree 会进入 slab 回收器，
 * 可能抢 zone lock / lru_lock，在自旋锁内调用是上下文违规。
 */
static void yat_detach_history_records(struct yat_cpu_history_table *tbl,
				      struct list_head *out)
{
	INIT_LIST_HEAD(out);

	spin_lock(&tbl->lock);
	list_splice_init(&tbl->time_list, out);
	tbl->nr_records = 0;
	spin_unlock(&tbl->lock);
}

/* 销毁历史表 */
static void yat_destroy_history_tables(struct yat_cpu_history_table *tables)
{
    struct yat_history_record *rec, *tmp;
    LIST_HEAD(doomed);
    int cpu;
    
    if (!tables)
        return;
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        yat_detach_history_records(&tables[cpu], &doomed);
        list_for_each_entry_safe(rec, tmp, &doomed, list) {
            list_del(&rec->list);
            kfree(rec);
        }
    }
}

/*
 * 添加历史记录。
 *
 * 上下文：可能从 balance_yat_casched()（持 rq_lock，可能关中断）以及
 * pick_next_task/put_prev_task（持 rq_lock）调用，所以分配必须是
 * GFP_ATOMIC 且不能失败后重试。
 *
 * 限流：链表长度超过 YAT_HISTORY_MAX_RECORDS 时，在**同一次插入**里
 * 顺手摘掉最旧的一批（链表按时间戳递增，头部最旧）。裁剪部分同样
 * 在锁外 kfree。
 */
static int yat_add_history_record(int cpu, int job_id, u64 timestamp, u64 exec_time)
{
    struct yat_history_record *rec, *old, *tmp;
    struct yat_cpu_history_table *tbl;
    LIST_HEAD(expired);
    bool need_trim = false;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return -EINVAL;
    
    tbl = &global_history_tables[cpu];
    
    rec = kmalloc(sizeof(*rec), GFP_ATOMIC);
    if (!rec)
        return -ENOMEM;
    
    rec->job_id = job_id;
    rec->timestamp = timestamp;
    rec->exec_time = exec_time;
    
    spin_lock(&tbl->lock);
    /* 直接插入链表尾部（时间戳递增） */
    list_add_tail(&rec->list, &tbl->time_list);
    tbl->nr_records++;
    
    if (tbl->nr_records > YAT_HISTORY_MAX_RECORDS) {
        /*
         * 一次裁掉 1/4，避免每次插入都裁一条导致锁内工作变成常量开销，
         * 也避免裁剪频率随负载线性上升。
         */
        unsigned int target = YAT_HISTORY_MAX_RECORDS * 3 / 4;
        unsigned int excess = tbl->nr_records - target;
        
        list_for_each_entry_safe(old, tmp, &tbl->time_list, list) {
            if (!excess)
                break;
            list_move_tail(&old->list, &expired);
            tbl->nr_records--;
            excess--;
        }
        need_trim = true;
    }
    spin_unlock(&tbl->lock);
    
    if (need_trim) {
        list_for_each_entry_safe(old, tmp, &expired, list) {
            list_del(&old->list);
            kfree(old);
        }
    }

    /*
     * 统计 + tracepoint 都放在锁外：本函数在持 rq_lock 的调度回调里运行，
     * 越短地持有 tbl->lock 越好；trace 事件的写入也不该被 tbl->lock 串行化，
     * 否则每个 CPU 的历史表锁会变成全局 trace 吞吐的瓶颈。
     */
    YAT_STAT_INC(history_records);
    trace_yat_casched_account(cpu, job_id, exec_time);
    
    return 0;
}

/* 查询某任务最近一次执行时间戳 */
static u64 yat_get_task_last_timestamp(int cpu, int job_id)
{
    u64 last_ts = 0;
    struct yat_history_record *rec;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return 0;
    
    spin_lock(&global_history_tables[cpu].lock);
    list_for_each_entry(rec, &global_history_tables[cpu].time_list, list) {
        if (rec->job_id == job_id && rec->timestamp > last_ts) {
            last_ts = rec->timestamp;
        }
    }
    spin_unlock(&global_history_tables[cpu].lock);
    
    return last_ts;
}

/* 计算时间区间内的执行时间总和 */
static u64 yat_sum_exec_time_since(int cpu, u64 start_ts, u64 end_ts)
{
    u64 sum = 0;
    struct yat_history_record *rec;
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return 0;
    
    spin_lock(&global_history_tables[cpu].lock);
    list_for_each_entry(rec, &global_history_tables[cpu].time_list, list) {
        if (rec->timestamp < start_ts)
            continue;
        if (rec->timestamp > end_ts)
            break;
        sum += rec->exec_time;
    }
    spin_unlock(&global_history_tables[cpu].lock);
    
    return sum;
}

/*
 * 清理过期的历史记录。
 *
 * 链表按时间戳递增，所以从头部扫到第一个未过期即可。
 * 同样把 kfree 移出临界区。
 */
void yat_cleanup_old_records(int cpu, u64 cutoff_time)
{
    struct yat_history_record *rec, *tmp;
    struct yat_cpu_history_table *tbl;
    LIST_HEAD(expired);
    
    if (cpu < 0 || cpu >= NR_CPUS)
        return;
    
    tbl = &global_history_tables[cpu];
    
    spin_lock(&tbl->lock);
    list_for_each_entry_safe(rec, tmp, &tbl->time_list, list) {
        if (rec->timestamp >= cutoff_time)
            break;
        
        list_move_tail(&rec->list, &expired);
        tbl->nr_records--;
    }
    tbl->last_cleanup_time = jiffies;
    spin_unlock(&tbl->lock);
    
    list_for_each_entry_safe(rec, tmp, &expired, list) {
        list_del(&rec->list);
        kfree(rec);
    }
}

/* 初始化加速表 */
int yat_accel_table_init(struct yat_accel_table *table)
{
    if (!table)
        return -EINVAL;
    
    table->hash_buckets = kcalloc(1 << YAT_ACCEL_HASH_BITS, sizeof(struct hlist_head), GFP_KERNEL);
    if (!table->hash_buckets)
        return -ENOMEM;
    
    /* 初始化所有哈希桶 */
    for (int i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        INIT_HLIST_HEAD(&table->hash_buckets[i]);
    }
    
    spin_lock_init(&table->lock);
    table->last_cleanup_time = jiffies;
    
    return 0;
}

/*
 * 销毁加速表。
 *
 * 原实现在 table->lock 里一边 hlist_del 一边 kfree，还顺手 kfree 了
 * hash_buckets 本身。kfree 会进 slab 回收器，可能抢 zone/lru 锁，
 * 在自旋锁内调用是上下文违规（lockdep 会报 invalid context）。
 *
 * 改成：持锁只做「把桶数组摘走」和「把全部条目挂到临时链表」，
 * 解锁后再统一释放条目与桶数组。
 */
void yat_accel_table_destroy(struct yat_accel_table *table)
{
    struct yat_accel_entry *entry;
    /*
     * 两个迭代器必须分开声明：hlist_for_each_entry_safe() 的游标类型是
     * struct hlist_node *，而 list_for_each_entry_safe() 的游标类型必须是
     * struct yat_accel_entry *。复用同一个变量会让编译器按第一种用法把
     * entry 推断成 hlist_node，随后报出「hlist_node 没有 lru_list 成员」。
     */
    struct hlist_node *tmp;
    struct yat_accel_entry *next;
    struct hlist_head *buckets;
    LIST_HEAD(doomed);
    int i;
    
    if (!table || !table->hash_buckets)
        return;
    
    spin_lock(&table->lock);
    buckets = table->hash_buckets;
    table->hash_buckets = NULL;
    for (i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        hlist_for_each_entry_safe(entry, tmp, &buckets[i], hash_node) {
            hlist_del(&entry->hash_node);
            /*
             * 借用 lru_list 把条目串到临时链表上 —— 条目此刻已脱离
             * 哈希表，不会再被查找路径看到，所以可以安全借道。
             */
            list_add_tail(&entry->lru_list, &doomed);
        }
    }
    spin_unlock(&table->lock);
    
    list_for_each_entry_safe(entry, next, &doomed, lru_list) {
        list_del(&entry->lru_list);
        kfree(entry);
    }
    kfree(buckets);
}

/*
 * 兼容性：保留原有的加速表操作函数（针对task_pid）
 */

/* 生成(task_pid, cpu_id)的哈希值 */
static u32 yat_accel_hash(pid_t task_pid, int cpu_id)
{
    return hash_32((u32)task_pid ^ (u32)cpu_id, YAT_ACCEL_HASH_BITS);
}

/* 查找加速表条目 */
static struct yat_accel_entry *yat_accel_find_entry(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    u32 hash_val = yat_accel_hash(task_pid, cpu_id);
    
    if (!table || !table->hash_buckets)
        return NULL;
    
    hlist_for_each_entry(entry, &table->hash_buckets[hash_val], hash_node) {
        if (entry->job_id == task_pid && entry->cpu_id == cpu_id)  /* 使用job_id字段 */
            return entry;
    }
    return NULL;
}

/*
 * 插入加速表条目。
 *
 * 原实现在 table->lock 内做 kmalloc(GFP_ATOMIC)，而本函数由
 * yat_find_best_core_for_job() 在 balance_yat_casched() 里按「每个
 * online CPU」调用一次 —— 64 核下一次全局调度就有 64 次锁内分配。
 *
 * 改为「先无锁分配 → 持锁插入 → 若发现并发插入则释放自己的副本」。
 * 这是内核里处理 "allocate before lock" 的标准模式：多分配一次远比
 * 在原子上下文里持锁分配便宜。
 */
int yat_accel_table_insert(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio)
{
    struct yat_accel_entry *entry;
    u32 hash_val;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    /*
     * 分配放在临界区之外。注意调用上下文是 balance_yat_casched()，
     * 持 rq_lock 且可能关中断，所以只能用 GFP_ATOMIC。
     */
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry)
        return -ENOMEM;
    
    entry->job_id = task_pid;  /* 兼容性：将task_pid存储在job_id字段中 */
    entry->cpu_id = cpu_id;
    entry->benefit = benefit;
    entry->wcet = wcet;
    entry->crp_ratio = crp_ratio;
    entry->last_update_time = jiffies;
    INIT_LIST_HEAD(&entry->lru_list);
    
    hash_val = yat_accel_hash(task_pid, cpu_id);
    
    spin_lock(&table->lock);
    /* 检查是否已存在（可能在分配期间被别的 CPU 插入了） */
    if (yat_accel_find_entry(table, task_pid, cpu_id)) {
        spin_unlock(&table->lock);
        kfree(entry);
        /* 重复条目不是错误，只说明该 (job, cpu) 快照已在表里 */
        YAT_STAT_INC(accel_dup);
        return -EEXIST;
    }
    hlist_add_head(&entry->hash_node, &table->hash_buckets[hash_val]);
    spin_unlock(&table->lock);

    YAT_STAT_INC(accel_inserts);
    
    return 0;
}

/* 更新加速表条目 */
int yat_accel_table_update(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio)
{
    struct yat_accel_entry *entry;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    spin_lock(&table->lock);
    
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (!entry) {
        spin_unlock(&table->lock);
        return -ENOENT;
    }
    
    entry->benefit = benefit;
    entry->wcet = wcet;
    entry->crp_ratio = crp_ratio;
    entry->last_update_time = jiffies;
    
    spin_unlock(&table->lock);
    return 0;
}

/* 查找benefit值 */
u64 yat_accel_table_lookup(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    u64 benefit = 0;
    
    if (!table || !table->hash_buckets)
        return 0;
    
    spin_lock(&table->lock);
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (entry)
        benefit = entry->benefit;
    spin_unlock(&table->lock);
    
    return benefit;
}

/* 删除加速表条目 */
int yat_accel_table_delete(struct yat_accel_table *table, pid_t task_pid, int cpu_id)
{
    struct yat_accel_entry *entry;
    
    if (!table || !table->hash_buckets)
        return -EINVAL;
    
    spin_lock(&table->lock);
    
    entry = yat_accel_find_entry(table, task_pid, cpu_id);
    if (!entry) {
        spin_unlock(&table->lock);
        return -ENOENT;
    }
    
    hlist_del(&entry->hash_node);
    spin_unlock(&table->lock);
    
    /* kfree 移到锁外：持自旋锁调用会进入 slab 回收器 */
    kfree(entry);
    return 0;
}

/* 清理过期条目（摘链与释放分离，同 yat_accel_table_destroy） */
void yat_accel_table_cleanup(struct yat_accel_table *table, u64 before_time)
{
    struct yat_accel_entry *entry;
    /* 同 yat_accel_table_destroy()：hlist 与 list 的游标类型不同，必须分开。 */
    struct hlist_node *tmp;
    struct yat_accel_entry *next;
    LIST_HEAD(doomed);
    int i;
    
    if (!table || !table->hash_buckets)
        return;
    
    spin_lock(&table->lock);
    for (i = 0; i < (1 << YAT_ACCEL_HASH_BITS); i++) {
        hlist_for_each_entry_safe(entry, tmp, &table->hash_buckets[i], hash_node) {
            if (time_before((unsigned long)entry->last_update_time, (unsigned long)before_time)) {
                hlist_del(&entry->hash_node);
                list_add_tail(&entry->lru_list, &doomed);
            }
        }
    }
    table->last_cleanup_time = jiffies;
    spin_unlock(&table->lock);
    
    list_for_each_entry_safe(entry, next, &doomed, lru_list) {
        list_del(&entry->lru_list);
        kfree(entry);
    }
}

/*
 * 历史表操作函数
 */

/*
 * 全局共享结构的一次性初始化。
 *
 * 调用时机：core.c 的 sched_init() 在 for_each_possible_cpu() 循环里对
 * **每个可能的 CPU** 调用 init_yat_casched_rq()。而 ready_pool / accel_table /
 * default_dag / global_history_tables 都是**全 CPU 共享**的，绝不能按 CPU 重复
 * 初始化 —— 原实现正是在这个循环里反复 kmalloc（只用 if 判 NULL 挡，存在
 * 分配失败后其它 CPU 反复重试并各自 fallback 出私有池的竞态），并且反复
 * 调用 yat_init_history_tables(global_history_tables) 把已经写入的历史记录
 * 链表头全部重置，等于隔一个 CPU 就丢弃一次统计。
 *
 * 上下文：sched_init() 处于 start_kernel() 的单线程、进程上下文（boot CPU，
 * 中断关闭但可睡眠），因此这里用 GFP_KERNEL 是合法的。
 */
static bool yat_global_init_done;

static void yat_global_init_once(void)
{
    if (READ_ONCE(yat_global_init_done))
        return;

    /* 1. 就绪作业池（全 CPU 共享，见锁序说明第 1 条） */
    if (!global_ready_pool) {
        global_ready_pool = kmalloc(sizeof(*global_ready_pool), GFP_KERNEL);
        if (global_ready_pool)
            yat_ready_pool_init(global_ready_pool);
        else
            pr_err("yat_casched: 无法分配全局就绪池，AJLR 全局调度将被跳过\n");
    }

    /* 2. 加速表（纯遥测结构，分配失败不影响调度正确性） */
    if (!global_accel_table) {
        global_accel_table = kmalloc(sizeof(*global_accel_table), GFP_KERNEL);
        if (global_accel_table)
            yat_accel_table_init(global_accel_table);
        else
            pr_warn("yat_casched: 无法分配全局加速表，遥测功能关闭\n");
    }

    /*
     * 3. 默认 DAG。
     *
     * 必须在这里创建：yat_ajlr_enqueue_task() 只做 READ_ONCE(global_default_dag)，
     * 不会再在原子上下文里惰性创建。没有这一句，所有任务的 p->yat_casched.job
     * 永远是 NULL，AJLR 路径完全失效。
     *
     * dag_id 传 0 表示"由 atomic 计数器分配"，避免与后续 yat_create_dag()
     * 手工传入的 ID 冲突。
     */
    if (!global_default_dag) {
        global_default_dag = __yat_create_dag(0, 0, GFP_KERNEL);
        if (!global_default_dag)
            pr_err("yat_casched: 无法创建默认 DAG，AJLR 路径将退化为纯 FIFO\n");
    }

    /* 4. 历史表（全 CPU 共享，下标是 CPU 号而不是 rq 私有副本） */
    yat_init_history_tables(global_history_tables);

    WRITE_ONCE(yat_global_init_done, true);
}

/*
 * 初始化 Yat-CAShed 运行队列 - AJLR 版本
 *
 * 本函数被 sched_init() 按 possible CPU 调用，所以必须区分：
 *   - 每 CPU 私有状态（tasks/nr_running/active_dags/...）：每次都初始化；
 *   - 全 CPU 共享状态（ready_pool/accel_table/default_dag/history）：
 *     走 yat_global_init_once()，只初始化一次。
 *
 * 注意：struct yat_casched_rq 里不再有 history_tables 私有副本，历史记录
 * 统一放在 global_history_tables[cpu] 中，避免 NR_CPUS² 的内存浪费。
 */
void init_yat_casched_rq(struct yat_casched_rq *rq)
{
    if (!rq)
        return;

    /* ---- 每 CPU 私有部分 ---- */
    INIT_LIST_HEAD(&rq->tasks);
    INIT_LIST_HEAD(&rq->active_dags);
    rq->nr_running = 0;
    rq->agent = NULL;
    rq->cache_decay_jiffies = jiffies;
    spin_lock_init(&rq->history_lock);

    /* ---- 全 CPU 共享部分：一次性初始化 ---- */
    yat_global_init_once();

    /* 共享指针在初始化之后才可能被读到，这里的赋值天然是单线程的 */
    rq->ready_pool = global_ready_pool;
    rq->accel_table = global_accel_table;
}

/*
 * 检查任务是否属于Yat-CAShed调度类
 */
bool yat_casched_prio(struct task_struct *p)
{
    return (p->policy == SCHED_YAT_CASCHED);
}

/*
 * 更新当前任务的运行时统计 - AJLR 版本
 *
 * 与 CFS 的 update_curr() 对齐，必须同时维护三样东西：
 *   1. se.exec_start      —— 下次计算 delta 的基准点；
 *   2. se.sum_exec_runtime —— 任务累计运行时间（**原实现漏了这一条**）；
 *   3. 本类私有的 vruntime —— 供优先级/时间片策略使用。
 *
 * 漏掉 (2) 的后果是连锁的：task_tick_yat_casched() 用
 *   runtime = p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime;
 * 判断时间片是否耗尽，而 sum_exec_runtime 永远是 0，于是 runtime 恒为 0，
 * 时间片检查永远不成立 —— Yat 任务在 tick 上永远不会被抢占，只能等它
 * 自己阻塞或退出。这是"调度器看起来能跑但行为完全不对"的典型症状。
 */
void update_curr_yat_casched(struct rq *rq)
{
    struct task_struct *curr = rq->curr;
    u64 now = rq_clock_task(rq);
    u64 delta_exec;

    if (curr->sched_class != &yat_casched_sched_class)
        return;

    delta_exec = now - curr->se.exec_start;
    if (unlikely((s64)delta_exec < 0))
        delta_exec = 0;

    if (unlikely(!delta_exec))
        return;

    curr->se.exec_start = now;
    curr->se.sum_exec_runtime += delta_exec;
    curr->yat_casched.vruntime += delta_exec;
}

/*
 * 为任务取得（必要时创建）关联作业。
 *
 * 上下文：调用者可能处于
 *   - select_task_rq()：持 p->pi_lock、中断关闭（try_to_wake_up 用
 *     raw_spin_lock_irqsave 取 pi_lock）；
 *   - enqueue_task()：持 rq_lock、可能关中断。
 * 两者都**不允许睡眠**，所以分配只能用 GFP_ATOMIC，失败就返回 NULL，
 * 调用方退化为「不参与 AJLR 决策、按原 CPU 运行」——
 * 这是可接受的降级，不影响调度正确性。
 *
 * 为什么要在这里创建而不是只在 enqueue_task 里创建：
 *   ttwu 路径的顺序是 select_task_rq() → set_task_cpu() → activate_task()
 *   → enqueue_task()。如果等到 enqueue_task 才有 job，那么选核阶段就
 *   拿不到 job，AJLR 的决策永远没有落地的机会。
 */
static struct yat_job *yat_get_or_create_job(struct task_struct *p)
{
    struct yat_job *job = p->yat_casched.job;
    struct yat_dag_task *default_dag;
    int user_priority;

    if (job)
        return job;

    default_dag = yat_get_default_dag();
    if (!default_dag)
        return NULL;

    /*
     * rt_priority 对 SCHED_YAT_CASCHED 任务由 sched_setscheduler() 按
     * sched_priority 写入（见 core.c 的 __setscheduler_params），
     * 未设置时为 0 == 中等优先级。
     */
    user_priority = p->rt_priority;

    job = __yat_create_job(p->pid, user_priority, YAT_DEFAULT_WCET_NS,
                           default_dag, GFP_ATOMIC);
    if (!job)
        return NULL;

    job->task = p;
    WRITE_ONCE(p->yat_casched.job, job);

    return job;
}

/*
 * select_task_rq - AJLR 决策的实际落地点。
 *
 * 为什么必须在这里做，而不是在 balance() 里做：
 *
 *   核心调度器调用本函数时（try_to_wake_up），持有 p->pi_lock 且中断
 *   关闭，并且**在本函数返回之后**会自己完成
 *     is_cpu_allowed() 校验 → set_task_cpu() → activate_task()
 *   三步。也就是说，「把任务放到哪颗核」这个动作由通用层免费提供，
 *   本函数唯一要做的就是**返回一个合法的 CPU 编号**。
 *
 *   相比之下在 balance_yat_casched() 里选核已经太晚：彼时任务已经
 *   在某个 rq 上，要改核就必须拿目标 rq 的 rq_lock 做跨核迁移，
 *   这既违反 A5 的加锁规则（不能在自己的 rq_lock 里阻塞等待别的
 *   rq_lock），也容易与热插拔/亲和性设置竞争。
 *
 * 只在 WF_TTWU（唤醒）路径上决策：
 *   - WF_FORK/WF_EXEC 时任务还没有任何执行历史，recency 全为 0，
 *     AJLR 的 benefit 排序退化为「所有核并列」，做决策只是浪费一次
 *     O(nr_cpus) 的历史表扫描；
 *   - 保持 fork/exec 的原有行为，减少不必要的迁移。
 *
 * 返回值：若算出的核心不在 p->cpus_ptr 内，核心调度器的
 * is_cpu_allowed() 会兜底选择允许的核，所以这里无需自己校验亲和性。
 */
int select_task_rq_yat_casched(struct task_struct *p, int task_cpu, int flags)
{
    struct yat_job *job;
    u64 now, last;
    int prev_core;
    int best_core;

    if (!(flags & WF_TTWU))
        return task_cpu;

    /*
     * 新建的作业此时还没有任何历史记录，yat_calculate_recency() 全 0，
     * benefit 处处相等，yat_find_best_core_for_job() 只会按 Impact
     * （目标核当前负载）挑一个。这本身是有意义的放置（把新任务放到
     * 最空闲的核），所以这里**不**跳过首次决策。
     */
    job = yat_get_or_create_job(p);
    if (!job)
        return task_cpu;

    /* 每次唤醒都刷新优先级：sched_setscheduler() 可能改了 sched_priority */
    WRITE_ONCE(job->job_priority, p->rt_priority);

    prev_core = READ_ONCE(job->assigned_cpu);

    /*
     * 决策节流 —— 见 struct yat_job.last_decision_ns 的注释。
     *
     * 这一次唤醒的绝大多数情况都会走这里返回，代价只有两次
     * READ_ONCE + 一次时间比较，不会把 O(nr_cpus^2) 的历史表扫描
     * 拖进 try_to_wake_up() 的关键路径。
     */
    if (prev_core != YAT_INVALID_CORE && cpu_online(prev_core)) {
        now = ktime_get_ns();

        /* (a) 目标核当前空闲：上次的决策依然成立，直接复用 */
        if (likely(!READ_ONCE(cpu_rq(prev_core)->yat_casched.nr_running)))
            goto out;

        /* (b) 距上次决策还不到最小间隔：保持粘性，避免抖动 */
        last = READ_ONCE(job->last_decision_ns);
        if (now - last < YAT_DECISION_MIN_INTERVAL_NS)
            goto out;
    } else {
        now = ktime_get_ns();
    }

    best_core = yat_find_best_core_for_job(job);
    if (best_core == YAT_INVALID_CORE)
        return task_cpu;   /* 保留 task_cpu，通用层会做 is_cpu_allowed 兜底 */

    /*
     * 记录「算法认为该去哪」。这是决策与执行之间唯一的交接点：
     * 若最终运行核与 assigned_cpu 不一致，说明通用层的亲和性/迁移
     * 约束覆盖了算法决策，debugfs 与 trace 可据此统计被否决的比例。
     */
    WRITE_ONCE(job->assigned_cpu, best_core);
    WRITE_ONCE(job->last_decision_ns, now);
    prev_core = best_core;

out:
    trace_yat_casched_wakeup_place(p->pid, job->job_id, task_cpu,
                                   prev_core, READ_ONCE(job->job_priority));

    return prev_core;
}

/*
 * 将任务加入运行队列 - AJLR版本
 *
 * 锁的归属：本函数由核心调度器在**持有 rq_lock(rq)** 的上下文里调用，
 * rq->yat_casched.tasks / nr_running 天然由 rq_lock 保护，因此这里
 * **不需要也不应该**再拿 yat_pool_lock。
 *
 * 原实现在这里 spin_lock(&yat_pool_lock) 之后直接 return 而没有 unlock，
 * 造成两处问题：
 *   - 第二次入队即自旋死锁（同一 CPU 重复获取非递归锁）；
 *   - 即使补上 unlock，yat_pool_lock 保护的是 yat_ready_job_pool，
 *     与 rq 运行队列无关，属于「用错误的锁保护数据」。
 *
 * 另外 yat_ajlr_enqueue_task() 需要在原子上下文里分配 job，见其实现。
 */
void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;

    /*
     * 初始化任务的 Yat-CAShed 实体。
     *
     * **不要在这里把 p->yat_casched.job 置 NULL。**
     * dequeue_task_yat_casched() 用 list_del_init() 退出 run_list，因此
     * 每次「睡眠 → 唤醒」循环后 run_list 都是空的。若把它当作「首次
     * 入队」的判据并顺手清空 job，那么每次唤醒都会新建一个 yat_job：
     *   - 旧 job 仍挂在 dag->jobs 上，永不释放 —— 每次唤醒泄漏一个
     *     struct yat_job，且 dag->total_jobs 无界增长（debugfs 可见）；
     *   - 旧 job 上的历史/加速表条目与新 job_id 对不上，recency 统计断裂。
     * 现在 job 的生命周期与任务绑定，只在 task_dead 时释放。
     */
    if (list_empty(&p->yat_casched.run_list)) {
        INIT_LIST_HEAD(&p->yat_casched.run_list);
        p->yat_casched.vruntime = 0;
        p->yat_casched.last_cpu = -1;  /* 使用-1表示未初始化 */
    }

    /* 添加到运行队列（rq_lock 已由调用者持有） */
    list_add_tail(&p->yat_casched.run_list, &yat_rq->tasks);
    yat_rq->nr_running++;

    /* AJLR处理：创建或更新作业信息 */
    yat_ajlr_enqueue_task(rq, p, flags);
}

/*
 * 将任务从运行队列移除 - AJLR版本
 *
 * 同上：rq->yat_casched.tasks 由 rq_lock 保护，不涉及 yat_pool_lock。
 */
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;

    if (!list_empty(&p->yat_casched.run_list)) {
        list_del_init(&p->yat_casched.run_list);
        yat_rq->nr_running--;
    }

    /* AJLR处理：更新作业状态 */
    yat_ajlr_dequeue_task(rq, p, flags);
}

/*
 * 选择下一个要运行的任务
 *
 * 说明（A5 审查结论）：
 *  - 这里**不做**任何历史记账。原实现在 pick_next_task 里以 exec_time=0
 *    插一条历史记录，等于凭空制造"0 长度执行窗口"，会直接拉低
 *    yat_sum_exec_time_since() 的均值，让 CRP 比率失真；真正的记账点在
 *    put_prev_task()（一次完整的 [开始, 结束] 区间）。
 *  - 调度类回调运行在 rq_lock 之下，这里只做 O(n) 扫描，不分配内存。
 *  - 只扫前 YAT_PICK_SCAN_LIMIT 个候选：yat_rq->tasks 已按入队顺序排列，
 *    头部是最活跃的任务，扫全部会让 pick 变成 O(队列长度)。
 *
 * 选择规则（两段式）：
 *   1. 优先选「AJLR 认为就该在本核运行」的任务，即
 *      assigned_cpu == rq->cpu，或尚未决策（YAT_INVALID_CORE）；
 *   2. 同等条件下按 job_priority 取最小（数值小 == 优先级高，与
 *      sched_priority 语义一致）。
 *   若扫描窗口内没有符合 1 的任务，则退化为「纯按优先级选」，
 *   保证本核不会因为决策偏好而空转。
 *
 * 为什么需要第 1 条：select_task_rq_yat_casched() 只能决定「任务进哪个
 * 运行队列」，进了之后同一核上仍可能有多个 Yat 任务竞争。若 pick 完全
 * 不看 assigned_cpu，那么「把 A 放到核 1」的决策在核 1 上有 A、B 两个
 * 任务时就可能被 B 顶掉，assigned_cpu 就只是个观测字段。这里让决策
 * 在竞争时拥有优先权，但用「两段式」保证它不会压过更紧急的任务
 * （更小的 job_priority 在同一个偏好段内仍然胜出）。
 */
#define YAT_PICK_SCAN_LIMIT	8

struct task_struct *pick_next_task_yat_casched(struct rq *rq)
{
    struct yat_casched_rq *yat_rq = &rq->yat_casched;
    struct task_struct *p = NULL;
    struct task_struct *fallback = NULL;
    struct sched_yat_casched_entity *yat_se;
    int best_priority = INT_MAX;
    int fallback_priority = INT_MAX;
    int scanned = 0;

    if (yat_rq->nr_running == 0 || list_empty(&yat_rq->tasks))
        return NULL;

    {
        struct list_head *pos;

        list_for_each(pos, &yat_rq->tasks) {
            struct task_struct *task;
            struct yat_job *job;
            int priority;
            int assigned;

            if (scanned >= YAT_PICK_SCAN_LIMIT)
                break;
            scanned++;

            yat_se = list_entry(pos, struct sched_yat_casched_entity, run_list);
            task = container_of(yat_se, struct task_struct, yat_casched);
            job = READ_ONCE(yat_se->job);
            priority = job ? READ_ONCE(job->job_priority) : 0;
            assigned = job ? READ_ONCE(job->assigned_cpu) : YAT_INVALID_CORE;

            /* 第一段：AJLR 指定本核（或尚未决策）且优先级最优 */
            if (assigned == rq->cpu || assigned == YAT_INVALID_CORE) {
                if (priority < best_priority) {
                    best_priority = priority;
                    p = task;
                }
            }

            /* 第二段：纯优先级兜底，只在第一段落空时才用 */
            if (priority < fallback_priority) {
                fallback_priority = priority;
                fallback = task;
            }
        }

        if (!p)
            p = fallback;

        if (!p) {
            yat_se = list_first_entry(&yat_rq->tasks,
                                     struct sched_yat_casched_entity,
                                     run_list);
            p = container_of(yat_se, struct task_struct, yat_casched);
        }
    }

    if (p) {
        p->yat_casched.last_cpu = rq->cpu;

        /*
         * trace 在 pick 之后、返回之前打点：此时 p 已经确定，rq->nr_running
         * 仍是本次 pick 看到的值（set_next_task 尚未扣减），语义上就是
         * "在这个队列长度下选中了谁"。job 可能为 NULL（任务尚未绑定作业），
         * 用 ?: 兜底成 0，避免解引用。
         */
        trace_yat_casched_pick(rq->cpu, p->pid,
                               p->yat_casched.job ? p->yat_casched.job->job_id : 0,
                               rq->yat_casched.nr_running);
    }

    return p;
}

/*
 * 设置下一个任务
 */
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first)
{
    p->se.exec_start = rq_clock_task(rq);
}

/*
 * 放置前一个任务（一次完整的执行区间在此结算）
 *
 * 顺序**必须**是「先算 delta，再 update_curr」：
 * update_curr_yat_casched() 会把 p->se.exec_start 推到 now，之后再用
 * now - exec_start 就只能得到 0。原实现正是这个顺序，导致历史表里
 * 每条记录的 exec_time 都是 0，yat_get_crp_ratio() 拿到的始终是
 * "零执行时间"，缓存热度判据整体失效。
 */
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    u64 now = rq_clock_task(rq);
    u64 delta_exec;

    delta_exec = now - p->se.exec_start;
    if (unlikely((s64)delta_exec < 0))
        delta_exec = 0;

    update_curr_yat_casched(rq);

    /* 更新历史表（AJLR 双向链表实现） */
    if (p->yat_casched.job && delta_exec > 0 && rq->cpu >= 0 && rq->cpu < NR_CPUS)
        yat_add_history_record(rq->cpu, p->yat_casched.job->job_id,
                               now, delta_exec);
}

/*
 * 任务时钟节拍处理 - 优化版本
 */
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued)
{
    struct sched_yat_casched_entity *yat_se = &p->yat_casched;
    u64 runtime, time_slice;
    
    update_curr_yat_casched(rq);
    
    /* 计算当前任务的运行时间 */
    runtime = p->se.sum_exec_runtime - p->se.prev_sum_exec_runtime;
    
    /* 基于优先级调整时间片：高优先级 = 更长时间片 */
    if (yat_se->job && yat_se->job->job_priority < 0) {
        time_slice = YAT_TIME_SLICE * 2;  /* 高优先级任务 */
    } else if (yat_se->job && yat_se->job->job_priority > 0) {
        time_slice = YAT_TIME_SLICE / 2;  /* 低优先级任务 */
    } else {
        time_slice = YAT_TIME_SLICE;      /* 普通优先级任务 */
    }
    
    /* 检查是否超过时间片 */
    if (runtime >= time_slice) {
        p->se.prev_sum_exec_runtime = p->se.sum_exec_runtime;
        resched_curr(rq);
    }
}

/*
 * 抢占检查
 */
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags)
{
    /* 简单实现：总是抢占 */
    resched_curr(rq);
}

/*
 * 负载均衡 —— 本调度类的全局调度入口
 *
 * 原实现只在 rq->cpu == 0 上执行。这在「所有 Yat 任务都被 pin 在 CPU0」
 * 的极端情况下等于没有调度：CPU0 一旦被一个长任务占住，balance 回调
 * 就不会再被触发，其它核上的就绪作业永远等不到分配。
 *
 * 改为「谁先到谁执行，用 trylock 保证单飞」：
 *   - balance 回调本身已持 rq_lock(本 CPU)，且可能关中断，所以只能
 *     spin_trylock —— 绝不能阻塞等待另一颗 CPU 上的全局调度结束；
 *   - 抢不到锁说明另一颗 CPU 正在做全局分配，本次直接返回，
 *     下一次 balance 会再来，不会丢作业；
 *   - 真正的数据竞争仍由 global_dag_lock / pool->lock 保护，
 *     yat_balance_lock 只是把「一整轮分配」做成原子操作，
 *     避免两个 CPU 同时把同一个作业算给不同核心。
 */
static DEFINE_SPINLOCK(yat_balance_lock);

int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    if (!spin_trylock(&yat_balance_lock)) {
        /*
         * 抢不到锁说明另一颗 CPU 正在做全局分配。这个计数是「争用强度」
         * 的直接指标：持续偏高意味着 balance 触发过于频繁，或单轮分配
         * 耗时过长，值得回头调 YAT_BALANCE_MAX_BATCH。
         */
        YAT_STAT_INC(balance_skips);
        return 0;
    }

    YAT_STAT_INC(balance_runs);
    yat_schedule_ajlr(&rq->yat_casched);

    spin_unlock(&yat_balance_lock);
    return 0;
}

/* 以下是其他必需的接口函数的简单实现 */

void yield_task_yat_casched(struct rq *rq)
{
    /* 让出CPU，将当前任务移到队列末尾 */
}

bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p)
{
    return false;
}

void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx)
{
    /* CPU亲和性设置 */
}

void rq_online_yat_casched(struct rq *rq)
{
    /* CPU上线处理 */
}

void rq_offline_yat_casched(struct rq *rq)
{
    /*
     * 只清理**本 CPU 自己**的那张历史表。
     *
     * 原实现调用 yat_destroy_history_tables(yat_rq->history_tables)
     * 会把该 rq 内嵌的 NR_CPUS 张表全部清空（内嵌副本已在 A5 中删除）。
     * 现在历史表只有全局一份，按 CPU 下标清理本核即可；
     * 全局结构（ready_pool / accel_table / default_dag）绝不在此释放。
     */
    int cpu = rq->cpu;

    if (cpu >= 0 && cpu < NR_CPUS) {
        struct yat_history_record *rec, *tmp;
        LIST_HEAD(doomed);

        yat_detach_history_records(&global_history_tables[cpu], &doomed);
        list_for_each_entry_safe(rec, tmp, &doomed, list) {
            list_del(&rec->list);
            kfree(rec);
        }
    }
}

struct task_struct *pick_task_yat_casched(struct rq *rq)
{
    return pick_next_task_yat_casched(rq);
}

void switched_to_yat_casched(struct rq *rq, struct task_struct *task)
{
    /* 任务切换到Yat-CAShed调度类时的处理 */
}

void prio_changed_yat_casched(struct rq *rq, struct task_struct *task, int oldprio)
{
    /*
     * 优先级变化后，作业的 job_priority 必须同步，否则下一次
     * yat_find_best_core_for_job() / pick_next_task 仍按旧优先级排序。
     * 这里不重排 run_queue：真正的顺序由 pick_next_task 每次现算，
     * 而就绪池里的作业会在下一次 balance 时按新优先级重新插位。
     */
    if (task && task->yat_casched.job)
        WRITE_ONCE(task->yat_casched.job->job_priority, task->rt_priority);
}

/*
 * task_dead - 任务消亡时释放其绑定的作业。
 *
 * 为什么必须有这个回调：
 *
 *   enqueue_task 不再在每次唤醒时新建作业（那会泄漏，见该函数注释），
 *   作业的生命周期改为「与 task_struct 同寿」。那么释放点只剩一个 ——
 *   任务真正消亡时。核心调度器在 finish_task_switch() 里检测到
 *   prev_state == TASK_DEAD 后会调用 sched_class->task_dead()，此时
 *   任务已经完成最后一次切换，不会再被调度，且 task_struct 仍然有效。
 *
 * 上下文：**不能睡眠**（处于调度器核心路径）。yat_destroy_job() 只做
 * 两次 spin_lock + kfree，没有睡眠式分配，符合要求。
 *
 * 注意这里必须先在同一 CPU 上把指针读出来再清空，避免重复释放：
 * task_dead 每个任务只会被调用一次，但清空指针能让之后任何残留路径
 * （例如 late dequeue）安全地看到 job == NULL 而不是悬垂指针。
 */
static void task_dead_yat_casched(struct task_struct *p)
{
    struct yat_job *job = READ_ONCE(p->yat_casched.job);

    if (!job)
        return;

    WRITE_ONCE(p->yat_casched.job, NULL);
    yat_destroy_job(job);
}

/*
 * Yat-CAShed调度类定义
 */
DEFINE_SCHED_CLASS(yat_casched) = {
    //.next_in_same_domain = NULL, // 根据内核版本决定是否需要
    .enqueue_task       = enqueue_task_yat_casched,
    .dequeue_task       = dequeue_task_yat_casched,
    .yield_task         = yield_task_yat_casched,
    .yield_to_task      = yield_to_task_yat_casched,
    .wakeup_preempt     = wakeup_preempt_yat_casched,
    .pick_next_task     = pick_next_task_yat_casched,
    .put_prev_task      = put_prev_task_yat_casched,
    .set_next_task      = set_next_task_yat_casched,
    .balance            = balance_yat_casched,
    .select_task_rq     = select_task_rq_yat_casched,
    .set_cpus_allowed   = set_cpus_allowed_yat_casched,
    .rq_online          = rq_online_yat_casched,
    .rq_offline         = rq_offline_yat_casched,
    .pick_task          = pick_task_yat_casched,
    .task_tick          = task_tick_yat_casched,
    .task_dead          = task_dead_yat_casched,
    .switched_to        = switched_to_yat_casched,
    .prio_changed       = prio_changed_yat_casched,
    .update_curr        = update_curr_yat_casched,
};

/*
 * Recency计算和核心调度算法
 */

/* 获取多个CPU上某任务的最近执行时间戳（取最大值） */
u64 yat_get_max_last_exec_time(int job_id, int *cpu_list, int cpu_count)
{
    u64 max_ts = 0, ts;
    int i;
    
    for (i = 0; i < cpu_count; i++) {
        ts = yat_get_task_last_timestamp(cpu_list[i], job_id);
        if (ts > max_ts)
            max_ts = ts;
    }
    
    return max_ts;
}

/* 计算多个CPU的执行时间总和 */
u64 yat_sum_exec_time_multi(int *cpu_list, int cpu_count, u64 start_ts, u64 end_ts)
{
    u64 total_sum = 0;
    int i;
    
    for (i = 0; i < cpu_count; i++) {
        total_sum += yat_sum_exec_time_since(cpu_list[i], start_ts, end_ts);
    }
    
    return total_sum;
}

/* 简化的CRP比率计算 */
u64 yat_get_crp_ratio(u64 recency)
{
    /* 简化实现：recency越大，CRP比率越大 */
    /* 返回千分比形式的比率，避免浮点运算 */
    if (recency == 0)
        return 0;
    
    /* 假设最大recency为某个阈值，进行归一化 */
    u64 max_recency = 1000000; /* 1秒，根据实际情况调整 */
    if (recency >= max_recency)
        return 1000; /* 100% */
    
    return (recency * 1000) / max_recency;
}

/*
 * 计算多层缓存 recency。
 *
 * 原实现每次调用都 kmalloc(NR_CPUS * sizeof(int), GFP_ATOMIC) 来构造一个
 * `all_cpus[i] = i` 的恒等数组，然后交给 yat_get_max_last_exec_time() /
 * yat_sum_exec_time_multi() 遍历。而这个函数又被
 * yat_find_best_core_for_job() 按「每个 online CPU」调用一次，于是
 * 一次全局调度就要做 nr_cpus * 2 次原子分配（64 核 = 128 次）。
 *
 * 这里直接改成对「当前 online CPU」原地遍历，不需要任何临时数组：
 * 分配次数从 O(nr_cpus^2) 降到 0。同时把范围从 NR_CPUS 收紧到
 * for_each_online_cpu()，离线 CPU 的历史表不再被无意义扫描。
 */
u64 yat_calculate_recency(int job_id, int cpu_id, u64 now)
{
    u64 l1_recency = 0, l2_recency = 0, l3_recency = 0;
    u64 l1_last, l2_last;
    int cpu;

    /* L1 层（私有缓存）：只看本 CPU */
    l1_last = yat_get_task_last_timestamp(cpu_id, job_id);
    if (l1_last > 0)
        l1_recency = yat_sum_exec_time_since(cpu_id, l1_last, now);

    /*
     * L2 层（共享缓存）：扫所有 online CPU，取「最近一次执行时间戳」
     * 的最大值，再累加该时间点之后的执行时间。
     */
    l2_last = 0;
    for_each_online_cpu(cpu) {
        u64 ts = yat_get_task_last_timestamp(cpu, job_id);

        if (ts > l2_last)
            l2_last = ts;
    }
    if (l2_last > 0) {
        for_each_online_cpu(cpu)
            l2_recency += yat_sum_exec_time_since(cpu, l2_last, now);
    }

    /* L3 层（全局共享缓存）：与 L2 相同，简化处理 */
    l3_recency = l2_recency;

    return l1_recency + l2_recency + l3_recency;
}

/*
 * 计算 Impact（简化实现）。
 *
 * 读取的是「别的 CPU」的 rq->yat_casched.nr_running，而本函数运行在
 * CPU0 的 rq_lock 之下，拿不到目标 CPU 的 rq_lock。这里用 READ_ONCE()
 * 做无锁读：该字段是 unsigned int，取值只用于比较大小，读到相邻两次
 * 更新之间的任意值都不影响正确性；但必须让编译器/工具链明确这不是
 * 一次可以随意合并或重排的普通读。
 */
u64 yat_calculate_impact(struct yat_job *job, int core)
{
    struct yat_casched_rq *yat_rq = &cpu_rq(core)->yat_casched;

    return (u64)READ_ONCE(yat_rq->nr_running) * job->wcet;
}

/*
 * 为作业寻找最佳核心 —— AJLR 核心算法。
 *
 * 原实现是「两轮 for_each_online_cpu + 用全局加速表在两轮之间传递
 * benefit」。三个问题：
 *
 *   1. 第一轮对**每个** online CPU 调一次 yat_accel_table_insert()，
 *      每次一次 kmalloc(GFP_ATOMIC) —— 64 核下每次全局调度 64 次
 *      原子分配，且全在持 rq_lock 的热路径上。
 *   2. 加速表被当作「同一函数内两轮之间的临时数组」使用，语义错位：
 *      它本应是跨调度周期复用的统计缓存，却被当栈变量使，条目被反复
 *      覆盖、命中率不具意义。
 *   3. yat_calculate_recency() 内部还有两次 for_each_online_cpu，
 *      两轮外层遍历 → 整体 O(nr_cpus^2) 次历史表加锁扫描。
 *
 * 改为单遍扫描：算出 benefit，相等时用 Impact 破平。收益：
 *   - 原子分配：nr_cpus → 0（只在最后为选中的核心写一条遥测）
 *   - 历史表扫描：2 * nr_cpus^2 → nr_cpus
 * 加速表只记录「最终选中的 (job, cpu, benefit, crp)」快照，回归它作为
 * 可观测性缓存的本来用途，不再承担控制流职责。
 */
int yat_find_best_core_for_job(struct yat_job *job)
{
    int best_core = YAT_INVALID_CORE;
    u64 max_benefit = 0;
    u64 min_impact = ULLONG_MAX;
    u64 best_crp_ratio = 0;
    u64 current_benefit, current_impact;
    u64 recency, crp_ratio;
    u64 now = ktime_get_ns();
    int cpu;
    
    for_each_online_cpu(cpu) {
        recency = yat_calculate_recency(job->job_id, cpu, now);
        crp_ratio = yat_get_crp_ratio(recency);
        current_benefit = ((1000 - crp_ratio) * job->wcet) / 1000;
        
        if (current_benefit < max_benefit)
            continue;
        
        if (current_benefit > max_benefit) {
            /* 新的最优 benefit：接管，Impact 立即可比 */
            max_benefit = current_benefit;
            best_crp_ratio = crp_ratio;
            best_core = cpu;
            min_impact = yat_calculate_impact(job, cpu);
            continue;
        }
        
        /* benefit 相同：用 Impact 破平，取更空闲的核心 */
        current_impact = yat_calculate_impact(job, cpu);
        if (current_impact < min_impact) {
            min_impact = current_impact;
            best_core = cpu;
        }
    }
    
    /*
     * 遥测：只记最终选中核心的快照。失败（-ENOMEM/-EEXIST）不影响
     * 调度决策 —— 调度器不依赖这张表。
     */
    if (best_core != YAT_INVALID_CORE && global_accel_table)
        yat_accel_table_insert(global_accel_table, job->job_id, best_core,
                               max_benefit, job->wcet, best_crp_ratio);

    /*
     * 与上面的 accel 写入刻意分开：accel 表可能因内存不足而失败，
     * 但「本次确实完成了分配」这一事实必须被统计到。jobs_placed 与
     * accel_inserts 的差值就是加速表分配失败的次数。
     */
    if (best_core != YAT_INVALID_CORE) {
        YAT_STAT_INC(jobs_placed);
        trace_yat_casched_place(job->job_id, best_core, max_benefit,
                                best_crp_ratio, job->wcet);
    }
    
    return best_core;
}

/*
 * 更新就绪池：遍历所有活动 DAG，把依赖已满足的作业投入就绪池。
 *
 * 锁序：global_dag_lock -> dag->lock -> pool->lock（yat_ready_pool_enqueue
 * 内部取 pool->lock），与文件头的加锁规则一致。
 *
 * 关于「原地修改导致遍历错乱」：yat_ready_pool_enqueue() 只修改
 * job->ready_list 与 job->state，**不会**动 dag->jobs 链表，
 * 因此在 list_for_each_entry(dag->jobs) 内部调用它是安全的
 * （它改的是另一条链表）。
 *
 * 上一版这里的 printk 已换成 pr_err/pr_warn 或直接吞掉：
 * 调度热路径上的 printk 会同步到控制台、可能触发 sleep，在持 rq_lock、
 * 可能关中断的上下文里既违规又严重扰动时序。
 */
void yat_update_ready_pool(struct yat_casched_rq *rq)
{
    struct yat_dag_task *dag;
    struct yat_job *job;
    struct yat_ready_job_pool *pool;

    if (!rq)
        return;

    pool = rq->ready_pool;
    if (!pool)
        return;

    spin_lock(&global_dag_lock);
    list_for_each_entry(dag, &global_dag_list, dag_list) {
        spin_lock(&dag->lock);
        list_for_each_entry(job, &dag->jobs, job_list) {
            /*
             * 跳过「隐式任务作业」。
             *
             * select_task_rq_yat_casched() 会为每个 SCHED_YAT_CASCHED
             * 任务在默认 DAG 里建一个作业，它代表的是「这个任务这次
             * 唤醒该放哪颗核」，其生命周期与任务绑定，而不是 DAG 依赖
             * 图里的一个节点。把它也扫进全局就绪池会导致：
             *   - 同一个任务被两条路径重复分配（select_task_rq 一次，
             *     yat_schedule_ajlr 又一次），第二次的 best_core 覆盖
             *     第一次的 assigned_cpu，trace 里出现自相矛盾的记录；
             *   - 每轮 balance 都把全部存活任务重新入池一次，池子被
             *     大量与「就绪」无关的条目灌满，YAT_BALANCE_MAX_BATCH
             *     很快耗尽，真正的 DAG 作业反而排不上。
             * 显式 DAG 作业（job->task == NULL，或由测试程序显式创建的
             * 带 task 的作业）仍照常进入就绪池。
             */
            if (job->task && job->dag == yat_get_default_dag())
                continue;

            if (job->state == YAT_JOB_WAITING && job->pending_predecessors == 0)
                yat_ready_pool_enqueue(pool, job);
        }
        spin_unlock(&dag->lock);
    }
    spin_unlock(&global_dag_lock);
}

/*
 * AJLR 主调度函数：把就绪池里的作业分配给核心。
 *
 * 修复的 TOCTOU：原实现先 yat_ready_pool_peek() 拿到队头作业，再
 * yat_find_best_core_for_job() 算核心（这一步要遍历所有 CPU 的历史表，
 * 可能耗时），最后才 yat_ready_pool_dequeue()。在 peek 与 dequeue 之间
 * 就绪池的队头完全可能被别的路径换掉 —— dequeue 弹出的会是**另一个**
 * 作业，而 best_core 是按前一个作业算出来的，结果是「作业 A 的核心
 * 分配记录写进了作业 B」。而且 peek 那次没弹出，循环里也没人计数，
 * 一旦队头作业找不到核心就直接 break，不会死循环，但已足够错配。
 *
 * 现在改为「一次性原子取出队头」：yat_ready_pool_dequeue() 内部持
 * pool->lock 完成 list_first_entry + list_del_init，返回的就是本次
 * 要处理的作业；找不到核心时用 enqueue 放回（enqueue 会按优先级重新
 * 插位并恢复 state=READY，语义等价于"未取出"）。
 */
void yat_schedule_ajlr(struct yat_casched_rq *rq)
{
    struct yat_job *job;
    struct yat_ready_job_pool *pool;
    int best_core;
    int guard = 0;

    if (!rq)
        return;

    pool = rq->ready_pool;
    if (!pool)
        return;

    /* 1. 更新就绪池 */
    yat_update_ready_pool(rq);

    /*
     * 2. 循环分配。
     *    guard 是防御性的：理论上每轮都会 dequeue 掉一个作业，
     *    但若某条路径忘了把 state 改回 WAITING，就绪池可能反复投喂
     *    同一个作业 —— 加个上限保证 balance 回调永不死循环，
     *    这比"调度器偶发挂死在软中断里"好得多。
     */
    while (guard++ < YAT_BALANCE_MAX_BATCH) {
        /* 3. 原子取出就绪池队头（不再是 peek + 后续 dequeue 两步） */
        job = yat_ready_pool_dequeue(pool);
        if (!job)
            break;

        /* 4. 为作业寻找最佳核心 */
        best_core = yat_find_best_core_for_job(job);

        if (best_core == YAT_INVALID_CORE) {
            /*
             * 找不到核心：放回就绪池，本轮结束（下次 balance 再试）。
             * 该计数持续增长说明「就绪作业数 > 可用核心数」，即过载，
             * 与 balance_skips 一起可用于判断是否该缩小批量上限。
             */
            YAT_STAT_INC(jobs_nocore);
            yat_ready_pool_enqueue(pool, job);
            break;
        }

        /*
         * 5. 分配完成。
         *
         * 这里**不再**写 yat_add_history_record(…, job->wcet)：
         * 那是把"预计执行时间"当成"已经执行了这么久"记进历史表，
         * 会让 yat_sum_exec_time_since() 凭空多出大量虚假执行时间，
         * CRP 比率被系统性拉偏。真实执行区间由 put_prev_task() 记账。
         */
        WRITE_ONCE(job->assigned_cpu, best_core);

        /*
         * 6. 决策落地。
         *
         * 本函数运行在 balance 回调里，持有的是「当前 CPU」的 rq_lock。
         * 跨核迁移需要目标 rq 的锁，在这里加锁会破坏
         * rq_lock -> global_dag_lock -> dag->lock -> pool->lock 的顺序，
         * 因此**不**在这里 set_task_cpu()。真正落地有两条路径：
         *   - 任务下次唤醒时由 select_task_rq_yat_casched() 返回本核；
         *   - 若任务此刻恰好在本核上可运行，触发一次本核重调度，
         *     让它在 pick_next_task 里按新的 assigned_cpu 重新竞争。
         * 原实现的 `best_core == raw_smp_processor_id()` 条件恒不成立
         * （best_core 是按负载挑出的核，很少正好是本核），等价于空语句，
         * 是一个「看起来在工作」的死分支。
         */
        if (job->task && task_cpu(job->task) == raw_smp_processor_id())
            resched_curr(cpu_rq(raw_smp_processor_id()));
    }
}

/*
 * AJLR调度器接口函数
 */

/* 为Linux任务创建关联的作业 */
struct yat_job *yat_create_job_for_task(struct task_struct *task, int job_priority, u64 wcet, struct yat_dag_task *dag)
{
    struct yat_job *job;
    
    if (!task || !dag)
        return NULL;
    
    job = yat_create_job(0, job_priority, wcet, dag);
    if (job) {
        job->task = task;
        task->yat_casched.job = job;
    }
    
    return job;
}

/* 完成任务对应的作业 */
void yat_complete_task_job(struct task_struct *task)
{
    struct yat_job *job;
    
    if (!task || !task->yat_casched.job)
        return;
    
    job = task->yat_casched.job;
    yat_job_complete(job);
    
    /* 清除关联 */
    task->yat_casched.job = NULL;
    job->task = NULL;
}

/*
 * 取「传统任务的默认 DAG」。
 *
 * 原实现是函数内 static 指针 + 惰性初始化：
 *
 *	static struct yat_dag_task *default_dag = NULL;
 *	if (!default_dag)
 *		default_dag = yat_create_dag(...);   // GFP_KERNEL
 *
 * 两个致命问题：
 *   1. 无锁 check-then-act：两个 CPU 同时首次入队会各建一个 DAG，
 *      其中一个指针被覆盖 → 泄漏，且它的作业永远不属于「默认 DAG」。
 *   2. yat_create_dag() 用 GFP_KERNEL，而本函数唯一的调用者
 *      yat_ajlr_enqueue_task() 跑在「持 rq_lock、可能关中断」的
 *      enqueue_task 回调里 → 睡眠式分配，直接触发
 *      "BUG: sleeping function called from invalid context"。
 *
 * 改为：默认 DAG 与其它全局结构一起在 init_yat_casched_rq() 的
 * 单线程初始化阶段建好（那里是进程上下文，GFP_KERNEL 合法）；
 * 本函数退化为一次无锁读。初始化失败时返回 NULL，调用者放弃绑定
 * 作业，而不是在原子上下文里重试分配。
 */
static struct yat_dag_task *yat_get_default_dag(void)
{
    return READ_ONCE(global_default_dag);
}

/*
 * 任务入队时的 AJLR 处理。
 *
 * 上下文：核心调度器持 rq_lock 调用 enqueue_task → 本函数。因此这里
 * **只能**用原子分配，且不应有 printk（每次调度都刷屏会严重扰动
 * 调度时序，A6 会改为 tracepoint）。原先的每任务 INFO 日志已移除。
 *
 * 关于 rt_priority：它是 RT 调度类的优先级字段，用
 * sched_setscheduler(policy=SCHED_YAT_CASCHED) 设置的任务并不会填它
 * （保持 0）。这里保留读取是为了兼容早期通过 rt_priority 传优先级的
 * 测试程序；对常规任务它等价于「中等优先级」，属已知局限。
 */
static void yat_ajlr_enqueue_task(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_job *job;

    /*
     * 统一走 yat_get_or_create_job()：它和 select_task_rq_yat_casched()
     * 共用同一套「查/建 + 绑 task + WRITE_ONCE 发布」逻辑，
     * WCET 也统一取 YAT_DEFAULT_WCET_NS。
     *
     * 原实现在这里硬编码 1000000 并自己写一遍 p->yat_casched.job = job，
     * 与 select_task_rq 里的建法不一致：两处若任一处改了 WCET 或忘了
     * WRITE_ONCE，就会出现「同一个任务在两个路径上看到的作业字段不同」。
     * 更糟的是两处都判断 `if (!p->yat_casched.job)`，并发唤醒时可能
     * 各自建一个作业，后写的覆盖先写的 —— 前者泄漏在 dag->jobs 上。
     */
    job = yat_get_or_create_job(p);
    if (!job)
        return;   /* 分配失败或默认 DAG 未就绪：任务照常参与调度 */

    /* 优先级可能被 sched_setscheduler() 改过，每次入队都同步一次 */
    WRITE_ONCE(job->job_priority, p->rt_priority);
}

/*
 * 任务出队时的 AJLR 处理。
 *
 * 这里**不再**添加历史记录。原因：本调度类把「本次运行了多久」的记账
 * 放在 put_prev_task_yat_casched() 里做（那里能拿到精确的
 * now - se.exec_start）。此处再按 sum_exec_runtime - prev_sum_exec_runtime
 * 记一条，等于把同一段执行时间统计两次，recency 被系统性放大，
 * yat_get_crp_ratio() 随之上偏，调度决策会被污染。
 *
 * 唯一的动作是把作业从全局就绪池摘掉，避免「任务已睡眠但作业仍挂在
 * pool->head 上」被 yat_schedule_ajlr() 反复选中。若作业此刻已是
 * RUNNING 状态，yat_ready_pool_dequeue() 已做过 list_del_init()，
 * yat_ready_pool_remove() 内部会检测空链表，是空操作。
 */
static void yat_ajlr_dequeue_task(struct rq *rq, struct task_struct *p, int flags)
{
    struct yat_job *job = p->yat_casched.job;

    if (!job)
        return;

    if (rq->yat_casched.ready_pool)
        yat_ready_pool_remove(rq->yat_casched.ready_pool, job);
}

/* 创建测试用的DAG和作业 */
void yat_create_test_dag(void)
{
    struct yat_dag_task *dag;
    struct yat_job *job1, *job2, *job3;
    
    /* 创建一个测试DAG */
    dag = yat_create_dag(1, 100); /* 高优先级DAG */
    if (!dag) {
        printk(KERN_ERR "yat_casched: Failed to create test DAG\n");
        return;
    }
    
    /* 创建三个作业 */
    job1 = yat_create_job(1, 50, 1000000, dag); /* 1ms WCET */
    job2 = yat_create_job(2, 40, 2000000, dag); /* 2ms WCET */
    job3 = yat_create_job(3, 30, 1500000, dag); /* 1.5ms WCET */
    
    if (job1 && job2 && job3) {
        /* 设置依赖关系：job1 -> job2 -> job3 */
        yat_job_add_dependency(job2, job1);
        yat_job_add_dependency(job3, job2);
        
        printk(KERN_INFO "yat_casched: Created test DAG with 3 jobs\n");
    } else {
        printk(KERN_ERR "yat_casched: Failed to create test jobs\n");
    }
}
