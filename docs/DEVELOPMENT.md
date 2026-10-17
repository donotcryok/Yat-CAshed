# 开发环境与构建指南

本指南描述 Yat-CAShed 三大模块（内核、模拟器、可视化/报告）的环境准备与常用命令。

## 1. 快速命令（顶层 Makefile）

仓库根目录新增了 [`Makefile`](../Makefile) 作为统一入口（对既有脚本的薄封装）：

```bash
make help              # 查看所有可用命令
make check             # 仓库健康检查（冲突标记、空文件、结构）
make symbol-check      # 内核头文件/实现符号一致性检查（静态）
make simulator-compile # 仅编译 Java 模拟器
make simulator-test    # 运行模拟器单元测试（确定性核心逻辑）
make simulator-quick   # 轻量可复现实验（快速）
make simulator-verify  # 编译 + 依赖自检 + 单元测试 + 快速实验
make simulator         # 全量运行 Java 模拟器（转发到 simulator/Makefile）
make visualize         # 生成模拟器可视化图表
make report            # 编译 LaTeX 论文（需 xelatex）
make clean             # 清理各类构建产物
```

> 内核编译是重量级操作（耗时数十分钟、需要数 GB 空间），未纳入顶层 `make all`，请按下文在内核目录内执行。

## 2. 内核模块（`linux-6.8-main/`）

### 依赖

```bash
sudo apt update
sudo apt install -y build-essential libncurses5-dev libssl-dev libelf-dev \
    bison flex bc dwarves fakeroot
sudo apt install -y qemu-system-x86 qemu-utils busybox-static cpio gzip
```

### 编译

```bash
cd linux-6.8-main

# 启用调度类
scripts/config --enable CONFIG_SCHED_CLASS_YAT_CASCHED

# 快速验证单文件（推荐，秒级反馈）
make kernel/sched/yat_casched.o

# 全量编译
make -j"$(nproc)"
```

### 运行测试（QEMU）

```bash
cd yat_test_scripts
./create_test_initramfs.sh       # 生成 initramfs
./start_test_environment.sh      # 启动 QEMU
# 退出：exit → Ctrl+A → X
```

> ✅ `kernel/sched/yat_casched.c` 的合并冲突已按「路线 A（决赛 AJLR 版）」解决，文件可直接参与编译。
> 决议与遗留项见 [CONFLICT_ANALYSIS.md](CONFLICT_ANALYSIS.md) §7–§8。
>
> ⚠️ **注意**：本仓库的 `linux-6.8-main/` 整树以 **CRLF** 行尾检入，`make tinyconfig` 会报
> `init/Kconfig: syntax error`；内核实编译请先 `sed -i 's/\r$//'` 或在干净内核树上套用补丁。

## 3. 模拟器（`simulator/`，Java 17+）

[`simulator/Makefile`](../simulator/Makefile) 已改写为**跨平台 GNU Make**（自动识别 OS、JDK、类路径分隔符），
在 Linux / macOS / WSL / Git-Bash 下均可直接使用；Windows 亦可沿用 `build.bat`。

```bash
# 方式一：仓库根目录（推荐）
make simulator-compile   # 编译（23 个类）
make simulator-quick     # 轻量回放：5 用例 / 20 任务 / 种子 42 / 跳过可视化
make simulator-verify    # 编译 + Apache Commons Math 自检 + 强制重编译

# 方式二：进入 simulator 目录
cd simulator
make help                # 查看全部目标
make compile             # 编译
make lib-test            # 依赖库自检
make test                # 单元测试（tests/YatSimulatorTest.java，58 断言）
make run-quick           # 轻量实验
make run                 # 全量实验（100 用例）+ 可视化 + 3 张 300dpi 图表
make picture             # 单独生成图表

# Windows
build.bat run
```

可调参数（详见 `make help` 与 [`README.md`](../README.md)）：
`--cores=`、`--cases=`、`--tasks=`、`--seed=`、`--no-viz`。

> 环境依赖：仅需 JDK 17+（`sudo apt install -y openjdk-17-jdk-headless`）。
> Maven/Gradle 非必需，第三方 JAR 已内置于 `simulator/lib/`。

可视化（Python 3 + matplotlib 必需，pandas/seaborn 可选）：

```bash
pip install -r simulator/visualizer/requirements.txt
cd simulator && python3 visualizer/visualize.py result/algorithm_comparison_results.csv
```

> Java 实验结束时会自动探测 `python3` → `python` 并调用 `visualizer/visualize.py`；
> 若均缺失则自动跳过，不影响数值结果与 CSV 导出。
> `visualize.py` 已兼容 matplotlib ≥ 3.9（`boxplot` 的 `labels` 参数改用 `set_xticklabels`）。
> 全量实验（`make run`）会产出 CSV 与 3 张图表至 `simulator/result/`（该目录已被 `.gitignore` 忽略）。

## 4. 论文（`report/paper/`）

```bash
# 需安装 TeX Live（含 xelatex，中文需 ctex 宏包）
cd report/paper
xelatex Yat-CAShed.tex
xelatex Yat-CAShed.tex   # 两遍以生成目录/引用
```

## 5. 仓库健康检查（CI 同款）

`.github/workflows/ci.yml` 会在 push / PR 时执行：

1. **仓库健康检查**：`bash scripts/check_repo.sh`（冲突标记、空文件、结构）。
2. **符号一致性检查**：`bash scripts/check_symbols.sh`（头文件声明 vs 实现定义、链接属性一致性）。
3. **冲突标记独立复核**：扫描 `<<<<<<<` / `>>>>>>>`（**无白名单**，任何冲突一律阻断）。
4. **模拟器构建与回放**：`make -C simulator compile` → `lib-test` → `test` → `run-quick`。

本地等价命令：

```bash
make check                     # 或 bash scripts/check_repo.sh
bash scripts/check_symbols.sh
make -C simulator verify
```

> 已知遗留项（`yat_global_pool_*` 7 个函数声明未实现）以**非阻断警告**形式输出，
> 便于在定稿前保持 CI 绿色，同时不掩盖新增问题。合并冲突已全部解决，不再有例外。

## 6. 命名与提交规范

- 项目名统一为 **Yat-Casched**（文档标题中可用 `Yat-CAShed`，但代码标识符统一 `yat_casched`）。
- 提交信息采用约定式前缀（沿用现有风格）：
  `feat:` / `fix:` / `docs:` / `chore:` / `refactor:` / `test:`。
- 详细贡献流程见 [`CONTRIBUTING.md`](../CONTRIBUTING.md)。
