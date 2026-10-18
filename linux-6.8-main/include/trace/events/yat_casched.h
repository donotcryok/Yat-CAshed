/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM yat_casched

#if !defined(_TRACE_YAT_CASCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_YAT_CASCHED_H

#include <linux/tracepoint.h>

/*
 * Yat_CASched 可观测性接口
 *
 * 为什么必须用 tracepoint 而不是 printk：
 *   调度类的 enqueue/dequeue/pick_next_task/balance 全部跑在持 rq_lock、
 *   可能关中断的上下文里。printk 会同步写控制台、可能触发唤醒与 sleep，
 *   在高频路径上既违反「原子上下文不得睡眠」的约束，又会因为偷走 CPU
 *   时间而系统性改变被测调度行为（观察者效应）。tracepoint 在未启用时
 *   只多一次静态分支判断，启用后写入 per-CPU 环形缓冲区，不做同步 I/O。
 *
 * 查看方式：
 *   cd /sys/kernel/tracing
 *   echo 1 > events/yat_casched/enable
 *   cat trace_pipe
 * 或用 perf / trace-cmd 采集后离线分析（A7 的 CFS vs Yat 对比即基于此）。
 */

/*
 * 作业被投入就绪池。
 * @pid:      关联任务的 pid（无关联任务时为 0）
 * @job_id:   作业 ID
 * @prio:     作业优先级（数值越小越优先）
 * @nr_jobs:  入队后就绪池中的作业数
 */
TRACE_EVENT(yat_casched_enqueue,

	TP_PROTO(int pid, int job_id, int prio, int nr_jobs),

	TP_ARGS(pid, job_id, prio, nr_jobs),

	TP_STRUCT__entry(
		__field(int, pid)
		__field(int, job_id)
		__field(int, prio)
		__field(int, nr_jobs)
	),

	TP_fast_assign(
		__entry->pid		= pid;
		__entry->job_id		= job_id;
		__entry->prio		= prio;
		__entry->nr_jobs	= nr_jobs;
	),

	TP_printk("pid=%d job_id=%d prio=%d nr_jobs=%d",
		  __entry->pid, __entry->job_id, __entry->prio,
		  __entry->nr_jobs)
);

/*
 * 作业被从就绪池取出准备分配。
 */
TRACE_EVENT(yat_casched_dequeue,

	TP_PROTO(int pid, int job_id, int nr_jobs),

	TP_ARGS(pid, job_id, nr_jobs),

	TP_STRUCT__entry(
		__field(int, pid)
		__field(int, job_id)
		__field(int, nr_jobs)
	),

	TP_fast_assign(
		__entry->pid		= pid;
		__entry->job_id		= job_id;
		__entry->nr_jobs	= nr_jobs;
	),

	TP_printk("pid=%d job_id=%d nr_jobs=%d",
		  __entry->pid, __entry->job_id, __entry->nr_jobs)
);

/*
 * AJLR 为一个作业选出目标核心。
 * @job_id:   作业 ID
 * @cpu:      被选中的核心
 * @benefit:  该核心上的缓存收益估算
 * @crp_ratio: CRP 比率（千分比，避免浮点）
 * @wcet:     作业的 WCET
 */
TRACE_EVENT(yat_casched_place,

	TP_PROTO(int job_id, int cpu, u64 benefit, u64 crp_ratio, u64 wcet),

	TP_ARGS(job_id, cpu, benefit, crp_ratio, wcet),

	TP_STRUCT__entry(
		__field(int, job_id)
		__field(int, cpu)
		__field(u64, benefit)
		__field(u64, crp_ratio)
		__field(u64, wcet)
	),

	TP_fast_assign(
		__entry->job_id		= job_id;
		__entry->cpu		= cpu;
		__entry->benefit	= benefit;
		__entry->crp_ratio	= crp_ratio;
		__entry->wcet		= wcet;
	),

	TP_printk("job_id=%d cpu=%d benefit=%llu crp_ratio=%llu wcet=%llu",
		  __entry->job_id, __entry->cpu,
		  (unsigned long long)__entry->benefit,
		  (unsigned long long)__entry->crp_ratio,
		  (unsigned long long)__entry->wcet)
);

/*
 * 唤醒选核：select_task_rq() 里真正落地 AJLR 决策的地方。
 *
 * 与 yat_casched_place 的区别：
 *   - yat_casched_place 由 yat_find_best_core_for_job() 在「评估候选核心」
 *     时发出，属于算法内部视角，可能被调用多次（每次全局复核都会重扫）；
 *   - 本事件只在 select_task_rq() 真正把返回值交给内核时发出一次，
 *     是「决策被采纳」的唯一凭据。prev_cpu 是任务睡眠前所在核，
 *     target_cpu 是本次选出的核 —— 两者不等才意味着真实迁移。
 *
 * @pid:        被唤醒的任务
 * @job_id:     其作业 ID
 * @prev_cpu:   唤醒前的 CPU（内核传入的 task_cpu）
 * @target_cpu: AJLR 选出的目标 CPU
 * @prio:       作业优先级快照
 */
TRACE_EVENT(yat_casched_wakeup_place,

	TP_PROTO(int pid, int job_id, int prev_cpu, int target_cpu, int prio),

	TP_ARGS(pid, job_id, prev_cpu, target_cpu, prio),

	TP_STRUCT__entry(
		__field(int, pid)
		__field(int, job_id)
		__field(int, prev_cpu)
		__field(int, target_cpu)
		__field(int, prio)
	),

	TP_fast_assign(
		__entry->pid		= pid;
		__entry->job_id		= job_id;
		__entry->prev_cpu	= prev_cpu;
		__entry->target_cpu	= target_cpu;
		__entry->prio		= prio;
	),

	TP_printk("pid=%d job_id=%d prev_cpu=%d target_cpu=%d prio=%d%s",
		  __entry->pid, __entry->job_id, __entry->prev_cpu,
		  __entry->target_cpu, __entry->prio,
		  __entry->prev_cpu == __entry->target_cpu ? " (stay)" : " (migrate)")
);

/*
 * pick_next_task 选中某个任务。
 * @cpu:        运行队列所在 CPU
 * @pid:        被选中的任务
 * @job_id:     其作业 ID（无作业为 -1）
 * @nr_running: 该类在本 CPU 上的就绪任务数
 */
TRACE_EVENT(yat_casched_pick,

	TP_PROTO(int cpu, int pid, int job_id, unsigned int nr_running),

	TP_ARGS(cpu, pid, job_id, nr_running),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, pid)
		__field(int, job_id)
		__field(unsigned int, nr_running)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->pid		= pid;
		__entry->job_id		= job_id;
		__entry->nr_running	= nr_running;
	),

	TP_printk("cpu=%d pid=%d job_id=%d nr_running=%u",
		  __entry->cpu, __entry->pid, __entry->job_id,
		  __entry->nr_running)
);

/*
 * 任务切出时写入历史表的一段执行区间。
 * 这是 CRP 的唯一真实数据来源；exec_time 为 0 的区间不会产生事件，
 * 因此本 tracepoint 的事件数应当与真实上下文切换次数同量级。
 */
TRACE_EVENT(yat_casched_account,

	TP_PROTO(int cpu, int job_id, u64 exec_time),

	TP_ARGS(cpu, job_id, exec_time),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, job_id)
		__field(u64, exec_time)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->job_id		= job_id;
		__entry->exec_time	= exec_time;
	),

	TP_printk("cpu=%d job_id=%d exec_time=%llu",
		  __entry->cpu, __entry->job_id,
		  (unsigned long long)__entry->exec_time)
);

#endif /* _TRACE_YAT_CASCHED_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
