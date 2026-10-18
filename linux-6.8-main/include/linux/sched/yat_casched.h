#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#ifdef CONFIG_SCHED_CLASS_YAT_CASCHED

/* 常量定义 */
#define YAT_INVALID_CORE        (-1)
#define YAT_MAX_DAG_PRIORITY    100

/* 作业状态枚举 */
enum yat_job_state {
    YAT_JOB_WAITING = 0,
    YAT_JOB_READY,
    YAT_JOB_RUNNING,
    YAT_JOB_COMPLETED
};

/* 加速表哈希位数 (2^8 = 256个桶，用于(task,cpu)组合) */
#define YAT_ACCEL_HASH_BITS    8
/* 历史表哈希位数 (2^7 = 128个桶，用于时间戳) */
#define YAT_HISTORY_HASH_BITS  7

/* 前向声明哈希表节点 */
struct hlist_node;

/* 加速表条目：存储(task,cpu)→benefit映射 */
struct yat_accel_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    /*
     * 仅供销毁/裁剪路径把「已从哈希表摘下的」条目串成临时链表，
     * 以便把 kfree 推迟到释放 table->lock 之后。正常查找永远不会碰它。
     */
    struct list_head lru_list;
    pid_t task_pid;                /* 任务PID */
    int job_id;                    /* 作业ID (兼容性) */
    int cpu_id;                    /* CPU编号 */
    u64 benefit;                   /* 加速值（benefit） */
    u64 wcet;                      /* 任务的WCET */
    u64 crp_ratio;                 /* CRP比率（千分比表示，避免浮点） */
    u64 last_update_time;          /* 最后更新时间 */
};

/* 加速表：二维哈希表，支持(task,cpu)→benefit查找 */
struct yat_accel_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 全局锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
};

/* 历史表条目：存储时间戳→任务映射 */
struct yat_history_entry {
    struct hlist_node hash_node;   /* 哈希表节点 */
    u64 timestamp;                 /* 时间戳作为键 */
    struct task_struct *task;      /* 任务指针 */
    u64 insert_time;               /* 插入时间 */
};

/* CPU历史表：每个CPU维护时间戳→任务的映射 */
struct yat_cpu_history_table {
    struct hlist_head *hash_buckets; /* 哈希桶数组 */
    spinlock_t lock;               /* 该CPU历史表的锁 */
    u64 last_cleanup_time;         /* 上次清理时间 */
    struct list_head time_list;    /* 时间链表 */
    /*
     * 链表长度。用于在插入路径上做软限流（见 yat_add_history_record）：
     * 原实现只增不删，一次长时间压测下来每条链表可以涨到几十万条，
     * 而 yat_get_task_last_timestamp()/yat_sum_exec_time_since() 每次
     * 都要全链表扫描 —— 持锁线性扫描 = 调度器 O(n) 抖动。
     */
    unsigned int nr_records;
};

/* 历史记录条目：基于链表的历史表实现 */
struct yat_history_record {
    struct list_head list;         /* 链表节点 */
    int job_id;                    /* 作业ID */
    u64 timestamp;                 /* 时间戳 */
    u64 exec_time;                 /* 执行时间 */
};

/* 就绪作业池 */
struct yat_ready_job_pool {
    struct list_head head;         /* 就绪作业链表头 */
    spinlock_t lock;               /* 就绪池锁 */
    unsigned int nr_jobs;          /* 就绪作业数量 */
};

/* DAG任务结构 */
struct yat_dag_task {
    struct list_head dag_list;     /* DAG链表节点 */
    struct list_head jobs;         /* 作业链表头 */
    int dag_id;                    /* DAG ID */
    int priority;                  /* DAG优先级 */
    int total_jobs;                /* 总作业数 */
    int completed_jobs;            /* 已完成作业数 */
    spinlock_t lock;               /* DAG锁 */
};

/* 作业结构 */
struct yat_job {
    struct list_head ready_list;   /* 就绪队列链表节点 */
    struct list_head job_list;     /* DAG作业链表节点 */
    struct list_head predecessors; /* 前驱作业列表 */
    struct list_head successors;   /* 后继作业列表 */
    int job_id;                    /* 作业ID */
    int job_priority;              /* 作业优先级 */
    enum yat_job_state state;      /* 作业状态 */
    int pending_predecessors;      /* 待完成的前驱作业数 */
    /*
     * AJLR 最近一次为该作业选定的目标核心（YAT_INVALID_CORE 表示尚未决策）。
     *
     * 这个字段是「决策」与「执行」之间的唯一交接点：
     *   - 写入者：select_task_rq_yat_casched()（唤醒时落地）与
     *             yat_schedule_ajlr()（全局复核时刷新）；
     *   - 读取者：debugfs 与 trace，用于判断「实际运行的核」是否等于
     *             算法选出的核（两者不等说明亲和性/迁移策略覆盖了决策）。
     * 它不参与锁保护：单写多读的普通 int，读到陈旧值只影响观测，不影响
     * 调度正确性。
     */
    int assigned_cpu;
    /*
     * 上一次为 assigned_cpu 做全量决策的时间（ktime_get_ns）。
     *
     * 决策本身是 O(nr_cpus^2)（yat_calculate_recency() 内部对每个 CPU
     * 再扫一遍 online CPU 的历史表），而 select_task_rq() 在每次
     * try_to_wake_up() 上都会调用。若不加节流，一次唤醒就要几千次
     * 哈希桶遍历，抖动会直接淹没调度收益。
     *
     * 因此决策遵循「粘性 + 最小间隔」：
     *   - 目标核仍空闲（nr_running == 0）→ 直接复用，不重算；
     *   - 距上次决策不足 YAT_DECISION_MIN_INTERVAL_NS → 复用；
     *   - 否则才重新跑 yat_find_best_core_for_job()。
     * 与 assigned_cpu 一样，普通字段，无锁保护，读到陈旧值只影响
     * 「这次要不要重算」的判断，不影响正确性。
     */
    u64 last_decision_ns;
    u64 wcet;                      /* 最坏情况执行时间 */
    /*
     * 原 per_cpu_last_ts[NR_CPUS] 已删除：它是纯写字段（三处写入、零处读取），
     * 却让每个 struct yat_job 常驻 NR_CPUS * 8 字节（64 核即 512B）。
     * "某 CPU 上该作业最后出现的时间" 统一由 global_history_tables[cpu]
     * 的链表查询得出（yat_get_task_last_timestamp），单一事实来源。
     */
    struct yat_dag_task *dag;      /* 所属DAG */
    struct task_struct *task;      /* 关联的任务 */
};

struct sched_yat_casched_entity {
    struct list_head run_list;      /* 运行队列链表节点 */
    u64 vruntime;                   /* 虚拟运行时间 */
    u64 slice;                      /* 时间片，与CFS保持一致 */
    int last_cpu;                   /* 上次运行的CPU */
    u64 per_cpu_recency[NR_CPUS];   /* 每个CPU的recency值 */
    u64 wcet;                       /* 任务的WCET */
    int priority;                   /* 任务优先级 */
    struct yat_job *job;            /* 关联的作业 */
};

struct yat_casched_rq {
    struct list_head tasks;         /* 任务队列 */
    struct list_head active_dags;   /* 活跃DAG列表 */
    unsigned int nr_running;        /* 运行任务数量 */
    struct task_struct *agent;     /* 代理任务 */
    u64 cache_decay_jiffies;       /* 缓存衰减时间 */
    spinlock_t history_lock;       /* 历史表锁 */
    
    /* 全局调度结构（指向全 CPU 共享实例，不拥有所有权） */
    struct yat_ready_job_pool *ready_pool;            /* 就绪作业池 */
    struct yat_accel_table *accel_table;              /* 加速表 */
    /*
     * 原实现内嵌了 history_tables[NR_CPUS]，即每个 rq 都持有一整套
     * NR_CPUS 张历史表 —— 而真正被使用的始终是全局的 global_history_tables，
     * 这份内嵌副本只在 rq_offline 里被销毁，纯属 NR_CPUS² 的内存浪费
     * （64 核约 64*(2*8+32+16) ≈ 40KB+rq）。已删除，历史表只保留全局一份。
     */
};

struct rq;
struct task_struct;
struct rq_flags;  /* 添加rq_flags的前向声明 */
struct affinity_context;  /* 添加affinity_context的前向声明 */

void init_yat_casched_rq(struct yat_casched_rq *rq);
bool yat_casched_prio(struct task_struct *p);
void update_curr_yat_casched(struct rq *rq);

/* 加速表接口 */
int yat_accel_table_init(struct yat_accel_table *table);
void yat_accel_table_destroy(struct yat_accel_table *table);
int yat_accel_table_insert(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio);
int yat_accel_table_update(struct yat_accel_table *table, pid_t task_pid, int cpu_id, u64 benefit, u64 wcet, u64 crp_ratio);
u64 yat_accel_table_lookup(struct yat_accel_table *table, pid_t task_pid, int cpu_id);
int yat_accel_table_delete(struct yat_accel_table *table, pid_t task_pid, int cpu_id);
void yat_accel_table_cleanup(struct yat_accel_table *table, u64 before_time);

/* DAG和作业管理接口 */
struct yat_dag_task *yat_create_dag(int dag_id, int priority);
void yat_destroy_dag(struct yat_dag_task *dag);
struct yat_job *yat_create_job(int job_id, int job_priority, u64 wcet, struct yat_dag_task *dag);
void yat_destroy_job(struct yat_job *job);
void yat_job_add_dependency(struct yat_job *job, struct yat_job *predecessor);
void yat_job_complete(struct yat_job *job);

/* Linux 任务 <-> AJLR 作业 绑定接口 */
struct yat_job *yat_create_job_for_task(struct task_struct *task, int job_priority,
					u64 wcet, struct yat_dag_task *dag);
void yat_complete_task_job(struct task_struct *task);

/* 内核态自测入口（调试用） */
void yat_create_test_dag(void);

/* AJLR算法接口 */
u64 yat_calculate_recency(int job_id, int cpu_id, u64 now);
u64 yat_calculate_impact(struct yat_job *job, int core);
int yat_find_best_core_for_job(struct yat_job *job);
void yat_schedule_ajlr(struct yat_casched_rq *rq);
void yat_update_ready_pool(struct yat_casched_rq *rq);
u64 yat_get_crp_ratio(u64 recency);
u64 yat_get_max_last_exec_time(int job_id, int *cpu_list, int cpu_count);
u64 yat_sum_exec_time_multi(int *cpu_list, int cpu_count, u64 start_ts, u64 end_ts);
void yat_cleanup_old_records(int cpu, u64 cutoff_time);

void enqueue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
void dequeue_task_yat_casched(struct rq *rq, struct task_struct *p, int flags);
struct task_struct *pick_next_task_yat_casched(struct rq *rq);
void set_next_task_yat_casched(struct rq *rq, struct task_struct *p, bool first);
void put_prev_task_yat_casched(struct rq *rq, struct task_struct *p);
void task_tick_yat_casched(struct rq *rq, struct task_struct *p, int queued);
void yield_task_yat_casched(struct rq *rq);
bool yield_to_task_yat_casched(struct rq *rq, struct task_struct *p);
void wakeup_preempt_yat_casched(struct rq *rq, struct task_struct *p, int flags);
int balance_yat_casched(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);
int select_task_rq_yat_casched(struct task_struct *p, int cpu, int flags);
void set_cpus_allowed_yat_casched(struct task_struct *p, struct affinity_context *ctx);
void rq_online_yat_casched(struct rq *rq);
void rq_offline_yat_casched(struct rq *rq);
struct task_struct *pick_task_yat_casched(struct rq *rq);
void switched_to_yat_casched(struct rq *rq, struct task_struct *p);
void prio_changed_yat_casched(struct rq *rq, struct task_struct *p, int oldprio);

extern const struct sched_class yat_casched_sched_class;

#else

static inline void init_yat_casched_rq(struct yat_casched_rq *rq) { }

#endif /* CONFIG_SCHED_CLASS_YAT_CASCHED */

#endif /* _LINUX_SCHED_YAT_CASCHED_H */
