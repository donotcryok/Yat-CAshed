# 内核补丁包（Yat-CASCHED）

本目录把 `linux-6.8-main/` 中相对上游 **linux v6.8** 的全部改动固化为补丁，
使 83k 文件 / 1.6GB 的内核整树**不必入库**。

## 文件

| 文件 | 内容 |
| --- | --- |
| `series` | 应用顺序（6 个补丁） |
| `0001-sched-yat-casched-core.patch` | 新增 `kernel/sched/yat_casched.c`（含 A6 的 tracepoint 与 debugfs）与 `include/linux/sched/yat_casched.h`、`include/trace/events/yat_casched.h` |
| `0002-sched-yat-casched-integration.patch` | 调度类接入：`core.c` / `sched.h` / `uapi/sched.h` / `init/main.c` / `init/Kconfig` / `init/init_task.c` / `kernel/sched/Makefile` / `vmlinux.lds.h` |
| `0003-x86-realmode-tolerate-missing-trampoline.patch` | `arch/x86/realmode/init.c`：trampoline 分配失败时降级而非 panic |
| `0004-kernel-gitignore.patch` | 内核树 `.gitignore`：修复 `*.s` 误伤 `.S`、`tags` 误伤源码目录，追加测试产物（含 `test_with_programs_build/`、`yat_test_scripts/results/`） |
| `0005-test-env.patch` | `boot_test_scripts/`（**只含 C 源码**：`yat_simple_test.c`、`yat_bench.c`）、`yat_test_scripts/`（**只含脚本**：含 `compare_cfs_vs_yat.sh`）、`test_visualization/`、三个 `initramfs/init` |
| `0006-docs.patch` | `Yat_Casched开发文档.md`、`yat_casched优化记录.md` |

> 二进制产物（`test_visualization/img/*.png`）与编译产物（`*.o`、`vmlinux`）
> **不入补丁**，它们是可再生成的实验结果，保存在仓库
> `docs/assets/` 或 CI 制品中。

## 应用

```bash
# 方式一：git（推荐，能保留上下文校验）
git clone --depth 1 --branch v6.8 \
  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git \
  linux-6.8-main
cd linux-6.8-main
git apply -p1 ../patches/0001-sched-yat-casched-core.patch
# ... 或一次性：
git apply -p1 $(sed 's|^|../patches/|' ../patches/series)

# 方式二：patch
for p in $(cat ../patches/series); do patch -p1 < "../patches/$p"; done
```

## 回退

```bash
for p in $(tac ../patches/series); do
  git apply -R -p1 "../patches/$p"     # 或 patch -R -p1 < "../patches/$p"
done
```

## 校验

```bash
( cd ../patches && sha256sum -c MANIFEST.sha256 )
```

## 一键准备

见仓库根目录 `scripts/setup-kernel.sh`（下载 v6.8 → 应用补丁 → 可选编译）。
