// SPDX-License-Identifier: GPL-2.0-only
/*
 * yat_bench.c —— CFS vs Yat_CASched 同一负载对比基准（guest 侧）
 *
 * 设计目标（对应 ROADMAP A7）：
 *   用**完全相同的计算量**在两种调度策略下跑一遍，输出一行 CSV，
 *   由宿主脚本收集后直接做配对比较。这里刻意不测"时间"，而是测
 *   "相同 work_units 下的墙钟时间"，否则不同策略下完成的工作量不同，
 *   比较就没有意义。
 *
 * 关键设计约束（都是踩过的坑）：
 *   1. 明确 pin 到同一个 CPU（默认 CPU0）。多核上 5 个任务各占一核
 *      根本不会发生调度竞争，测出来的差异全是噪声。
 *   2. 用固定迭代次数而不是"跑满 N 毫秒"。按时间跑会让"调度慢"
 *      的实现在同样的墙钟里完成更少的工作，指标不可比。
 *   3. 子进程通过 pipe 回传结构化数据，每个子进程一次 write，
 *      总长度远小于 PIPE_BUF，保证原子性，父进程无需额外同步。
 *   4. 统计上下文切换次数（读 /proc/self/status），这是调度策略
 *      差异最直接的证据之一 —— Yat 的缓存亲和性设计预期会显著
 *      减少迁移与切换。
 *   5. 输出到 stdout 的每一行都以 "YATCSV," 开头，宿主端按前缀
 *      过滤，guest 内核的其它日志（printk / dmesg）不会污染结果。
 *
 * 编译： gcc -O2 -static -o yat_bench yat_bench.c
 * 用法： ./yat_bench --policy cfs|yat [--tasks N] [--iters M] [--cpu C]
 *
 * 退出码：
 *   0 成功；2 参数错误；3 未能在当前内核设置 Yat 策略（内核未启用该调度类）
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SCHED_YAT_CASCHED
#define SCHED_YAT_CASCHED 8
#endif

/* 与 include/uapi/linux/sched.h 保持一致；老 glibc 可能没有这两个宏 */
#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

#define MAX_TASKS 64

/*
 * 子进程回传记录。
 * 只用固定宽度的整数类型，保证父子进程之间的 ABI 不依赖编译器选择。
 */
struct bench_record {
	uint32_t task_id;
	uint32_t padding;
	uint64_t wall_ns;	/* 本任务从开始到结束的墙钟时间 */
	uint64_t work_units;	/* 实际完成的计算量（应为 --iters 的值） */
	uint64_t vol_ctx;	/* 自愿上下文切换 */
	uint64_t invol_ctx;	/* 非自愿上下文切换 */
	int32_t  sched_ok;	/* 1 = 成功设置目标策略 */
	int32_t  error;		/* 非 0 表示本任务失败，附带 errno */
};

struct bench_options {
	int tasks;
	long iters;
	int cpu;
	int use_yat;
};

/*
 * 固定的计算负载。
 *
 * 用整数乘加而不是 sleep/IO，原因：本基准关心的是**CPU 时间片如何
 * 被分配与迁移**，任何阻塞都会让任务主动让出 CPU，把调度策略的
 * 差异掩盖掉。
 *
 * volatile + 依赖链让编译器无法把循环优化掉，也不用担心不同 GCC
 * 版本把工作量优化成常数时间。
 */
static uint64_t do_work(long iters)
{
	volatile uint64_t acc = 1;
	uint64_t local = acc;
	long i;

	for (i = 0; i < iters; i++) {
		/*
		 * 交替乘加，制造一条不可折叠的依赖链。
		 * 乘数取奇数常量，避免编译器识别出恒等/幂等模式。
		 */
		local = local * 6364136223846793005ULL + 1442695040888963407ULL;
		local ^= (local >> 33);
	}

	acc = local;
	return acc;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * 从 /proc/self/status 读取上下文切换计数。
 *
 * 直接解析文本而不是用 getrusage：ru_nvcsw / ru_nivcsw 在
 * 部分架构的 RUSAGE_SELF 语义下会把子进程计入，而我们要的是
 * "本进程自己被切换了多少次"。status 里的两个字段语义明确。
 */
static void read_ctx_switches(uint64_t *vol, uint64_t *invol)
{
	char buf[4096];
	ssize_t n;
	char *line, *saveptr;

	*vol = 0;
	*invol = 0;

	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0)
		return;

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = '\0';

	line = strtok_r(buf, "\n", &saveptr);
	while (line) {
		if (strncmp(line, "voluntary_ctxt_switches:", 24) == 0)
			*vol = strtoull(line + 24, NULL, 10);
		else if (strncmp(line, "nonvoluntary_ctxt_switches:", 27) == 0)
			*invol = strtoull(line + 27, NULL, 10);
		line = strtok_r(NULL, "\n", &saveptr);
	}
}

static int pin_to_cpu(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	/*
	 * sched_setaffinity 的 mask 必须覆盖一个**在线** CPU，否则
	 * 返回 EINVAL。调用者也可能是被内核迁移过来的，所以两个
	 * 参数都用 0（当前线程）。
	 */
	return sched_setaffinity(0, sizeof(set), &set);
}

/*
 * 写满一条记录。
 *
 * write() 对 pipe 来说小于 PIPE_BUF 时是原子的，但**不保证**一次写完全部
 * 字节（信号中断时会短写）。这里循环补齐，避免父子之间出现半条记录。
 * 返回值被真正使用，也顺手消除了 -Wunused-result。
 */
static int write_record(int fd, const struct bench_record *rec)
{
	const char *p = (const char *)rec;
	size_t left = sizeof(*rec);

	while (left > 0) {
		ssize_t n = write(fd, p, left);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		left -= (size_t)n;
	}

	return 0;
}

static int apply_policy(int use_yat)
{
	struct sched_param param;

	memset(&param, 0, sizeof(param));
	param.sched_priority = 0;

	/*
	 * 用 SCHED_OTHER 而不是 SCHED_NORMAL：后者在 glibc 里被
	 * __USE_MISC 守卫，静态链接或非默认 feature 宏组合下可能不可见；
	 * SCHED_OTHER 是 POSIX 常量，任何实现都有。
	 */
	if (!use_yat)
		return sched_setscheduler(0, SCHED_OTHER, &param);

	return sched_setscheduler(0, SCHED_YAT_CASCHED, &param);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"用法: %s [选项]\n"
		"  --policy cfs|yat   目标调度策略（默认 cfs）\n"
		"  --tasks N          并发任务数（默认 4，上限 %d）\n"
		"  --iters M          每任务固定迭代次数（默认 20000000）\n"
		"  --cpu C            全部任务 pin 到的物理 CPU（默认 0）\n"
		"  --help             显示本帮助\n",
		prog, MAX_TASKS);
}

static int parse_args(int argc, char **argv, struct bench_options *opt)
{
	int i;

	opt->tasks = 4;
	opt->iters = 20000000L;
	opt->cpu = 0;
	opt->use_yat = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 1;
		} else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
			const char *p = argv[++i];
			if (strcmp(p, "cfs") == 0)
				opt->use_yat = 0;
			else if (strcmp(p, "yat") == 0)
				opt->use_yat = 1;
			else {
				fprintf(stderr, "未知策略: %s\n", p);
				return -1;
			}
		} else if (strcmp(argv[i], "--tasks") == 0 && i + 1 < argc) {
			opt->tasks = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
			opt->iters = atol(argv[++i]);
		} else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
			opt->cpu = atoi(argv[++i]);
		} else {
			fprintf(stderr, "无法识别的参数: %s\n", argv[i]);
			return -1;
		}
	}

	if (opt->tasks <= 0 || opt->tasks > MAX_TASKS) {
		fprintf(stderr, "任务数必须在 1..%d 之间\n", MAX_TASKS);
		return -1;
	}
	if (opt->iters <= 0) {
		fprintf(stderr, "迭代次数必须为正\n");
		return -1;
	}
	if (opt->cpu < 0) {
		fprintf(stderr, "CPU 号必须非负\n");
		return -1;
	}

	return 0;
}

static void child_main(int fd, int task_id, const struct bench_options *opt)
{
	struct bench_record rec;
	uint64_t t0, t1;

	memset(&rec, 0, sizeof(rec));
	rec.task_id = (uint32_t)task_id;
	rec.work_units = 0;

	/*
	 * 先设策略再 pin，最后才开始计时：把 sched_setaffinity /
	 * sched_setscheduler 的系统调用开销排除在测量窗口之外。
	 */
	if (pin_to_cpu(opt->cpu) != 0) {
		rec.error = errno;
		(void)write_record(fd, &rec);
		_exit(1);
	}

	if (apply_policy(opt->use_yat) != 0) {
		/*
		 * 内核未启用 SCHED_CLASS_YAT_CASCHED 时这里会返回 EINVAL。
		 * 把 errno 带回父进程，让它以专门的状态码退出 —— 宿主脚本
		 * 据此判断"内核没编进这个调度类"，而不是把失败当成性能数据。
		 */
		rec.error = errno;
		(void)write_record(fd, &rec);
		_exit(1);
	}
	rec.sched_ok = 1;

	t0 = now_ns();
	(void)do_work(opt->iters);
	t1 = now_ns();

	rec.wall_ns = t1 - t0;
	rec.work_units = (uint64_t)opt->iters;
	read_ctx_switches(&rec.vol_ctx, &rec.invol_ctx);

	if (write_record(fd, &rec) != 0)
		_exit(1);
	_exit(0);
}

int main(int argc, char **argv)
{
	struct bench_options opt;
	struct bench_record recs[MAX_TASKS];
	const char *policy_name;
	pid_t pids[MAX_TASKS];
	int pipefd[2];
	int ret, i, got = 0;
	uint64_t sum_wall = 0, min_wall = UINT64_MAX, max_wall = 0;
	uint64_t sum_vol = 0, sum_invol = 0, sum_work = 0;
	int sched_failed = 0;

	ret = parse_args(argc, argv, &opt);
	if (ret != 0)
		return ret > 0 ? 0 : 2;

	policy_name = opt.use_yat ? "yat" : "cfs";

	if (pipe(pipefd) != 0) {
		perror("pipe");
		return 2;
	}

	for (i = 0; i < opt.tasks; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			/* 已经 fork 出去的子进程仍会写 pipe，父进程按 got 处理 */
			break;
		}
		if (pid == 0) {
			close(pipefd[0]);
			child_main(pipefd[1], i, &opt);
			_exit(1); /* 不可达 */
		}
		pids[i] = pid;
	}

	/*
	 * 父进程必须关掉写端：否则子进程全部退出后 read() 仍会阻塞，
	 * 因为管道里还有父进程自己持有的写端引用。这是 pipe 编程里
	 * 最经典的一处死锁。
	 */
	close(pipefd[1]);

	while (got < opt.tasks) {
		ssize_t n = read(pipefd[0], &recs[got], sizeof(recs[got]));

		if (n == 0)
			break;			/* 所有写端已关闭 */
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			break;
		}
		if ((size_t)n != sizeof(recs[got])) {
			/*
			 * 短读意味着某个子进程在 write 中途崩溃。丢弃这条
			 * 不完整记录，继续读下一条 —— 宁可少一个样本，
			 * 也不要把半截数据当成测量结果。
			 */
			fprintf(stderr, "警告: 收到不完整记录 (%zd 字节)\n", n);
			continue;
		}
		got++;
	}
	close(pipefd[0]);

	for (i = 0; i < opt.tasks; i++)
		(void)waitpid(pids[i], NULL, 0);

	if (got == 0) {
		fprintf(stderr, "错误: 没有收到任何测量记录\n");
		return 2;
	}

	for (i = 0; i < got; i++) {
		if (recs[i].error != 0) {
			sched_failed = 1;
			fprintf(stderr,
				"任务 %u 失败: errno=%d (%s)%s\n",
				recs[i].task_id, recs[i].error,
				strerror(recs[i].error),
				recs[i].sched_ok ? "" : " —— 设置调度策略失败");
			continue;
		}
		sum_wall += recs[i].wall_ns;
		if (recs[i].wall_ns < min_wall)
			min_wall = recs[i].wall_ns;
		if (recs[i].wall_ns > max_wall)
			max_wall = recs[i].wall_ns;
		sum_vol += recs[i].vol_ctx;
		sum_invol += recs[i].invol_ctx;
		sum_work += recs[i].work_units;
	}

	if (sched_failed) {
		fprintf(stderr,
			"错误: 无法在 %d 个任务上应用策略 '%s'。"
			"请确认内核配置了 CONFIG_SCHED_CLASS_YAT_CASCHED=y。\n",
			got, policy_name);
		return 3;
	}

	if (min_wall == UINT64_MAX)
		min_wall = 0;

	/*
	 * 输出格式（单行 CSV，数值单位见列名）：
	 *   YATCSV,policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,
	 *          sum_work_units,vol_ctx,invol_ctx,cpus_online
	 *
	 * 全部用整数纳米秒，避免浮点在 guest/host 之间出现格式化差异。
	 */
	printf("YATCSV,%s,%d,%ld,%d,%llu,%llu,%llu,%llu,%llu,%llu,%ld\n",
	       policy_name, opt.tasks, opt.iters, opt.cpu,
	       (unsigned long long)sum_wall,
	       (unsigned long long)min_wall,
	       (unsigned long long)max_wall,
	       (unsigned long long)sum_work,
	       (unsigned long long)sum_vol,
	       (unsigned long long)sum_invol,
	       sysconf(_SC_NPROCESSORS_ONLN));
	fflush(stdout);

	return 0;
}
