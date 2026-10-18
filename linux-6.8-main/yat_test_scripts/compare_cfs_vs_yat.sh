#!/bin/bash
#
# CFS vs Yat-CASched 同负载自动对比
# ============================================================
#
# 目标（A7）：
#   在**同一个内核**、**同一份负载**、**同一颗 CPU** 上，只改变调度策略，
#   得到两行可比的 CSV，并直接给出配对差值。这样"Yat 到底有没有用"不再
#   依赖人肉观察 dmesg，而是一条可复现、可入 CI 的命令。
#
# 为什么需要专用 initramfs：
#   通用的 create_test_initramfs.sh 生成的是**交互式菜单**，需要人工输入，
#   无法在 CI/无人值守场景下自动出数。本脚本因此自带一份非交互 init：
#   挂载 proc/sys → 依次跑 cfs 与 yat → poweroff。两份 initramfs 的
#   boot_test_scripts/*.c 编译规则保持一致（gcc -O2 -static）。
#
# 为什么两个策略要各跑一次 QEMU（而不是一次启动内跑两遍）：
#   1) 策略切换会残留上一轮的热状态（页表、cache、buddy 碎片），第二轮
#      天然占优；分两次启动让两轮都从"刚启动"的相同初始态出发。
#   2) 一次启动跑两遍还需在 guest 内做清理，复杂且更容易引入偏差。
#   代价是多一次 QEMU 启动（TCG 下约数秒），换来的是可比性，值得。
#
# 输出：
#   yat_test_scripts/results/cfs_vs_yat.csv   —— 两行原始数据 + 表头
#   yat_test_scripts/results/qemu_<policy>.log —— 两次启动的完整串口日志
#   标准输出：配对对比摘要（墙钟比、上下文切换差）
#
# 内核参数的两个坑（实测得出，勿随意改回）：
#   1) 必须用 arch/x86/boot/bzImage，不能用 ELF 的 vmlinux：QEMU 的 -kernel
#      对 x86 ELF 需要 PVH note，本项目内核没有，会直接报
#      "Error loading uncompressed kernel without PVH ELF Note" 而不启动。
#   2) 必须保留 ACPI，不要加 acpi=off / noapic / nolapic：-kernel 直接引导
#      bzImage 时绕过了 BIOS，AP 的枚举只能靠 ACPI MADT；一旦关掉 ACPI，
#      内核只能看到 1 个 CPU（nr_cpu_ids:1），而"单核上比调度策略"没有意义。
#      保留 ACPI 后实测 4 核正常起来，且 poweroff 能真正让 QEMU 退出。
#
# 用法：
#   ./compare_cfs_vs_yat.sh                          # 默认 4 任务 / 2e7 迭代 / CPU0
#   ./compare_cfs_vs_yat.sh --tasks 8 --iters 50000000
#   ./compare_cfs_vs_yat.sh --kernel ../arch/x86/boot/bzImage --timeout 300
#
# 退出码：0 成功；1 前置检查/运行失败；3 guest 未能在内核上启用 Yat 调度类。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TASKS=4
ITERS=20000000
CPU=0
CPUS=4
# 默认用 bzImage 而非 vmlinux，原因见文件头注释第 1 条。
KERNEL="$KERNEL_DIR/arch/x86/boot/bzImage"
TIMEOUT=180
OUT_DIR="$SCRIPT_DIR/results"
RESULT_CSV="$OUT_DIR/cfs_vs_yat.csv"

usage() {
    sed -n '2,45p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tasks)   TASKS="$2"; shift 2 ;;
        --iters)   ITERS="$2"; shift 2 ;;
        --cpu)     CPU="$2"; shift 2 ;;
        --cpus)    CPUS="$2"; shift 2 ;;
        --kernel)  KERNEL="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --out)     RESULT_CSV="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "未知参数: $1（用 --help 查看用法）" >&2; exit 2 ;;
    esac
done

# ---------------------------------------------------------------
# 0. 前置检查（失败要早，且要说清怎么修）
# ---------------------------------------------------------------
fail() { echo "❌ $*" >&2; exit 1; }

command -v qemu-system-x86_64 >/dev/null 2>&1 || \
    fail "未找到 qemu-system-x86_64（请安装 qemu-system-x86）"
command -v gcc >/dev/null 2>&1 || fail "未找到 gcc"
command -v cpio >/dev/null 2>&1 || fail "未找到 cpio"
command -v gzip >/dev/null 2>&1 || fail "未找到 gzip"

# 若用户显式给了 vmlinux，自动改指同目录构建出的 bzImage（并说明原因）。
case "$KERNEL" in
    */vmlinux)
        echo "提示: QEMU 不能直接引导 ELF vmlinux，改用 $(dirname "$KERNEL")/arch/x86/boot/bzImage" >&2
        KERNEL="$(dirname "$KERNEL")/arch/x86/boot/bzImage"
        ;;
esac

[ -f "$KERNEL" ] || fail "内核不存在: $KERNEL（先 cd $KERNEL_DIR && make -j\$(nproc)）"
[ -f /bin/busybox ] || fail "未找到 /bin/busybox（请安装 busybox-static）"
[ -f "$KERNEL_DIR/boot_test_scripts/yat_bench.c" ] || \
    fail "未找到 $KERNEL_DIR/boot_test_scripts/yat_bench.c"

# 内核是否真的编进了 Yat 调度类：先查 .config，能省下一次必然失败的 QEMU 启动
if [ -f "$KERNEL_DIR/.config" ] && \
   ! grep -q '^CONFIG_SCHED_CLASS_YAT_CASCHED=y' "$KERNEL_DIR/.config"; then
    fail "当前 .config 未启用 CONFIG_SCHED_CLASS_YAT_CASCHED=y，无从对比。
   请在 $KERNEL_DIR 中启用该项后重新编译（或运行 scripts/setup-kernel.sh）。"
fi

mkdir -p "$OUT_DIR"

# ---------------------------------------------------------------
# 1. 编译基准程序（输出到 results/ 下的临时目录，不污染源码目录）
# ---------------------------------------------------------------
BUILD_DIR="$OUT_DIR/build"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "=== CFS vs Yat-CASched 对比 ==="
echo "内核:     $KERNEL"
echo "负载:     tasks=$TASKS iters=$ITERS cpu=$CPU cpus=$CPUS"
echo ""

echo "🔧 编译 yat_bench ..."
if ! gcc -O2 -static -Wall -Wextra \
        -o "$BUILD_DIR/yat_bench" \
        "$KERNEL_DIR/boot_test_scripts/yat_bench.c"; then
    fail "yat_bench.c 编译失败"
fi

# ---------------------------------------------------------------
# 2. 生成非交互 initramfs
# ---------------------------------------------------------------
INITRAMFS_DIR="$BUILD_DIR/initramfs"
mkdir -p "$INITRAMFS_DIR"/{bin,proc,sys,dev,tmp}

cp /bin/busybox "$INITRAMFS_DIR/bin/busybox"
chmod +x "$INITRAMFS_DIR/bin/busybox"
cp "$BUILD_DIR/yat_bench" "$INITRAMFS_DIR/bin/yat_bench"
chmod +x "$INITRAMFS_DIR/bin/yat_bench"

# guest 侧要跑的策略由内核命令行参数选择：yatbench=cfs / yatbench=yat。
# 为什么用 cmdline 而不是把策略写死进 init：
#   同一份 initramfs 可以被多次启动复用，避免"多份 init"产生不一致。
cat > "$INITRAMFS_DIR/init" << 'INIT_EOF'
#!/bin/busybox sh
# 无人值守 init：跑一遍基准，然后关机。
# 不启 shell —— 本环境唯一目的就是产出 YATCSV 行。

/bin/busybox mount -t proc proc /proc 2>/dev/null
/bin/busybox mount -t sysfs sysfs /sys 2>/dev/null
/bin/busybox mount -t devtmpfs devtmpfs /dev 2>/dev/null
# debugfs 是 A6 可观测性的入口；挂载失败不影响基准，只是少一段证据。
/bin/busybox mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null

# 解析内核命令行里的 yatbench=<policy>
policy="cfs"
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        yatbench=*) policy="${arg#yatbench=}" ;;
    esac
done

# tasks/iters/cpu 通过 benchmark=<n>,<m>,<c> 传入
tasks=4
iters=20000000
cpu=0
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        benchmark=*)
            v="${arg#benchmark=}"
            tasks="${v%%,*}"
            rest="${v#*,}"
            iters="${rest%%,*}"
            cpu="${rest#*,}"
            ;;
    esac
done

echo "YATBENCH-BEGIN policy=$policy tasks=$tasks iters=$iters cpu=$cpu"
/bin/yat_bench --policy "$policy" --tasks "$tasks" --iters "$iters" --cpu "$cpu"
rc=$?
echo "YATBENCH-EXIT status=$rc"
echo "YATBENCH-END"

# A6 运行时证据：debugfs 节点若存在，把内容原样打到串口。
# 这些计数是 per-CPU 累加值，与调度行为是否真的发生直接相关 ——
# 若 jobs_placed / accel_inserts 恒为 0，说明 Yat 调度类根本没被调用过，
# 此时"性能更好"只可能是没走调度路径的假象。
if [ -d /sys/kernel/debug/yat_casched ]; then
    echo "YATDEBUG-BEGIN"
    for f in stats ready_pool dags; do
        echo "--- $f ---"
        /bin/busybox cat "/sys/kernel/debug/yat_casched/$f" 2>/dev/null
    done
    echo "YATDEBUG-END"
fi

# 同步文件系统并关机。-f 跳过 init 脚本重入；内核命令行已带 panic=1。
/bin/busybox sync
/bin/busybox poweroff -f
INIT_EOF
chmod +x "$INITRAMFS_DIR/init"

IMAGE="$BUILD_DIR/compare_initramfs.cpio.gz"
( cd "$INITRAMFS_DIR" && find . | cpio -o -H newc 2>/dev/null | gzip > "$IMAGE" )
echo "✅ initramfs: $IMAGE"
echo ""

# ---------------------------------------------------------------
# 3. 跑两次 QEMU（cfs / yat），只保留 YATCSV 行
# ---------------------------------------------------------------
# 内核在本次启动里报出的严重问题。这些行一旦出现，本轮数据不可信 ——
# KASAN 报告 / lockdep 环路 / oops 都可能让某一轮"看起来很快"（例如
# 任务其实被 abort 了），所以必须显式失败，而不是把数字照抄进 CSV。
check_kernel_splats() {
    local log="$1"
    local pattern='BUG:|Kernel panic|KASAN:|UBSAN:|possible circular locking|WARNING:.*at |Oops:|general protection fault|watchdog: BUG'

    if grep -nEq "$pattern" "$log"; then
        echo "  ❌ 内核日志中发现异常（本轮数据不可信）:" >&2
        grep -nE "$pattern" "$log" | head -10 >&2
        return 1
    fi
    return 0
}

run_one() {
    local policy="$1"
    local log="$OUT_DIR/qemu_${policy}.log"

    # 注意：本函数的标准输出**只能**是那一行 YATCSV —— 调用方用
    # $(run_one ...) 捕获它。所有进度/诊断信息必须走 stderr，否则会污染数据。
    echo "▶ 运行 QEMU: policy=$policy（smp=${CPUS}，超时 ${TIMEOUT}s）..." >&2

    # -no-reboot 防止 panic 后反复重启；timeout 兜住内核卡死。
    # 串口输出直接写日志文件（不 tee），便于失败后完整回看。
    #
    # 内核参数说明（本文件头有更详细的踩坑记录）：
    #   保留 ACPI → 只有靠 ACPI MADT 才能枚举 AP，否则 nr_cpu_ids 退化成 1；
    #   nokaslr  → 固定地址，日志更好读，也避免 TCG 下 KASLR 带来的差异；
    #   panic=1  → 出错立刻重启（配合 -no-reboot 即立刻退出），不要挂死等超时。
    timeout "$TIMEOUT" qemu-system-x86_64 \
        -kernel "$KERNEL" \
        -initrd "$IMAGE" \
        -m 1024 \
        -smp "$CPUS" \
        -nographic \
        -no-reboot \
        -accel tcg \
        -append "console=ttyS0,115200 panic=1 nokaslr yatbench=${policy} benchmark=${TASKS},${ITERS},${CPU}" \
        > "$log" 2>&1

    local rc=$?
    if [ $rc -eq 124 ]; then
        echo "  ⚠️  QEMU 超时（${TIMEOUT}s）：见 $log" >&2
    fi

    # 先查内核 splat，再取数据：避免把"崩溃前的最后一行"当成有效测量。
    check_kernel_splats "$log" || return 1

    local line
    line="$(grep -m1 '^YATCSV,' "$log" || true)"
    if [ -z "$line" ]; then
        echo "  ❌ 未在日志中找到 YATCSV 行: $log" >&2
        echo "     末尾 25 行：" >&2
        tail -25 "$log" >&2
        # 退出码 3 = guest 明确报告"无法应用该策略"
        if grep -q 'YATBENCH-EXIT status=3' "$log"; then
            return 3
        fi
        return 1
    fi

    if ! grep -q 'YATBENCH-EXIT status=0' "$log"; then
        echo "  ❌ guest 报告基准程序非零退出（见 $log）" >&2
        grep -E 'YATBENCH-EXIT|失败|错误' "$log" | head -5 >&2
        return 1
    fi

    # 把 A6 的 debugfs 计数器回显到结果里，并标注「AJLR 是否真的参与了调度」。
    # 实测：普通任务（sched_setscheduler 到 SCHED_YAT_CASCHED）只会在
    # yat_ajlr_enqueue_task() 里绑定一个 job，**不会**进入 global_ready_pool；
    # pick_next 走的是 rq->yat_casched.tasks。因此 jobs_placed==0 是当前实现的
    # 真实状态，不是脚本或内核故障 —— 但必须提醒读者不要把两件事混为一谈：
    # 本轮测到的是「调度类切换」的效果，不是「AJLR 作业分配」的效果。
    if grep -q '^YATDEBUG-BEGIN' "$log"; then
        echo "  ── A6 计数器 (policy=$policy) ──" >&2
        sed -n '/YATDEBUG-BEGIN/,/^--- ready_pool ---/p' "$log" \
            | grep -E 'balance_|jobs_|history_|accel_|nr_cpu_ids' \
            | sed 's/^/     /' >&2
        local placed
        placed="$(sed -n '/^jobs_placed/,+0p' "$log" | awk '{print $2}' | head -1)"
        if [ "${placed:-x}" = "0" ]; then
            echo "  ℹ️  jobs_placed=0：本轮未触发 AJLR 作业放置（见脚本内注释）。" >&2
            echo "      这表示差异来自「调度类切换」，而非 AJLR 分配算法。" >&2
        fi
    fi

    # 单核下"调度策略对比"没有意义，必须显式指出而不是默默产出数字。
    # 注意必须 tr -d '\r'：串口是 CRLF，残留的 \r 会让 [ -lt ] 报
    # "integer expression expected"。
    local online
    online="$(printf '%s' "$line" | tr -d '\r' | cut -d, -f12)"
    if [ "${online:-0}" -lt 2 ] 2>/dev/null; then
        echo "  ⚠️  guest 只有 ${online} 个在线 CPU，对比结果不具备参考价值。" >&2
        echo "      检查是否误加了 acpi=off/noapic/nolapic（见脚本头注释）" >&2
    fi

    printf '%s\n' "$line"
    return 0
}

CFS_LINE="$(run_one cfs)" || fail "CFS 轮次失败"
# 单独区分退出码 3：不是脚本问题，而是目标内核没有这个调度类。
YAT_LINE="$(run_one yat)"
YAT_RC=$?
if [ $YAT_RC -eq 3 ]; then
    fail "目标内核未启用 CONFIG_SCHED_CLASS_YAT_CASCHED（guest 报 EINVAL）。
   当前 .config 检查通过的话，请确认 vmlinux/bzImage 确实是用该 .config 编出来的。"
elif [ $YAT_RC -ne 0 ]; then
    fail "Yat 轮次失败（rc=$YAT_RC）"
fi

# ---------------------------------------------------------------
# 4. 落盘 CSV
# ---------------------------------------------------------------
{
    echo "policy,tasks,iters,cpu,sum_wall_ns,min_wall_ns,max_wall_ns,sum_work_units,vol_ctx,invol_ctx,cpus_online"
    printf '%s\n' "$CFS_LINE" | sed 's/^YATCSV,//'
    printf '%s\n' "$YAT_LINE" | sed 's/^YATCSV,//'
} > "$RESULT_CSV"

# ---------------------------------------------------------------
# 5. 配对摘要
# ---------------------------------------------------------------
# 字段位置（去掉 YATCSV 前缀后）：
#   1 policy  2 tasks  3 iters  4 cpu  5 sum_wall  6 min_wall  7 max_wall
#   8 sum_work  9 vol_ctx  10 invol_ctx  11 cpus_online
field() { printf '%s' "$1" | tr -d '\r' | sed 's/^YATCSV,//' | cut -d, -f"$2"; }

C_POLICY=$(field "$CFS_LINE" 1);  Y_POLICY=$(field "$YAT_LINE" 1)
C_WALL=$(field "$CFS_LINE" 5);    Y_WALL=$(field "$YAT_LINE" 5)
C_MIN=$(field "$CFS_LINE" 6);     Y_MIN=$(field "$YAT_LINE" 6)
C_MAX=$(field "$CFS_LINE" 7);     Y_MAX=$(field "$YAT_LINE" 7)
C_VOL=$(field "$CFS_LINE" 9);     Y_VOL=$(field "$YAT_LINE" 9)
C_INV=$(field "$CFS_LINE" 10);    Y_INV=$(field "$YAT_LINE" 10)

# awk 做浮点：纯 shell 没有可靠的浮点运算。
read -r WALL_RATIO MIN_RATIO MAX_RATIO < <(awk -v cw="$C_WALL" -v yw="$Y_WALL" \
                                              -v cm="$C_MIN"  -v ym="$Y_MIN" \
                                              -v cx="$C_MAX"  -v yx="$Y_MAX" \
    'BEGIN{ printf "%.4f %.4f %.4f\n", (cw?yw/cw:0), (cm?ym/cm:0), (cx?yx/cx:0) }')

echo ""
echo "=========================================="
echo " 配对对比结果"
echo "=========================================="
printf ' %-14s %-16s %-16s\n' "指标" "$C_POLICY" "$Y_POLICY"
printf ' %-14s %-16s %-16s\n' "sum_wall_ns" "$C_WALL" "$Y_WALL"
printf ' %-14s %-16s %-16s\n' "min_wall_ns" "$C_MIN" "$Y_MIN"
printf ' %-14s %-16s %-16s\n' "max_wall_ns" "$C_MAX" "$Y_MAX"
printf ' %-14s %-16s %-16s\n' "vol_ctx" "$C_VOL" "$Y_VOL"
printf ' %-14s %-16s %-16s\n' "invol_ctx" "$C_INV" "$Y_INV"
echo "------------------------------------------"
echo " sum_wall 比 (yat/cfs): $WALL_RATIO   （<1 表示 Yat 更快）"
echo " min_wall 比 (yat/cfs): $MIN_RATIO"
echo " max_wall 比 (yat/cfs): $MAX_RATIO"
echo " 上下文切换差 (yat-cfs): vol=$((Y_VOL - C_VOL))  invol=$((Y_INV - C_INV))"
echo "------------------------------------------"
echo " CSV: $RESULT_CSV"
echo " 日志: $OUT_DIR/qemu_cfs.log  $OUT_DIR/qemu_yat.log"
echo ""
echo "注意：TCG 模拟下单次测量噪声较大，结论请以多次重复的中位数/分布为准，"
echo "      不要用单次 sum_wall 的小幅差异当作算法结论。"
echo "=========================================="
