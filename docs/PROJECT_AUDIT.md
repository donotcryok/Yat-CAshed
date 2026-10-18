# 工程化审计快照

> 审计时间：2026-10-18。范围：仓库根、`docs/`、`scripts/`、`simulator/`、`report/`、
> `linux-6.8-main/` 中**本项目新增**的部分（不含上游内核代码本身的质量评价）。
>
> 本文只记录**事实与状态**；可选方向与取舍建议见 [`DIRECTIONS.md`](DIRECTIONS.md)。

---

## 1. 规模与结构现状

| 项目 | 数值 | 说明 |
|------|------|------|
| 工作区体积 | ≈2.1 GB | 绝大部分是内核源码 |
| `.git` 体积 | ≈529 MB | 历史中含大二进制 |
| 受版本控制文件数 | 83,568 | 其中内核树 83,499 |
| 本项目自有文件 | 约 69 个 | 其余均为 Linux 6.8 上游文件 |
| 模拟器运行时依赖 | 1 个 JAR（2.2 MB） | 已从 30 个精简到 1 个 |
| 模拟器单元测试 | 102 条断言 | `make -C simulator test` |
| 内核接口符号 | 29 声明 / 27 非 static 定义 / 28 static | 全部一致（A/B/C 三类不一致均为 0） |

**结论**：代码主体（调度类 + 模拟器）体量很小，但仓库被整棵内核树与大二进制主导，
导致克隆、检索、评审成本远超实际代码量。

> **状态更新（A8 实跑后）**：审计时「AJLR 决策不落地（`jobs_placed` 恒 0）」这一条
> 已关闭。实跑数据为 `ttwu_ok 512 / select_decided 254 / jobs_placed 254 /
> place_hit 925 · miss 0 · pending 14`，接管率 98%；但墙钟仍为 CFS 的约 1.9–2.2 倍，
> 即「决策已生效、性能未站住」。详见 [`ROADMAP.md`](ROADMAP.md) §A8.1–§A8.2。

---

## 2. 已发现的问题与处理状态

### 2.1 构建链路

| 编号 | 问题 | 影响 | 状态 |
|------|------|------|------|
| B-01 | `report/paper/Yat-CAShed.tex` 使用 `\input{chapters/...}`，而实际章节文件与主文件平铺 | `make report` 必然失败 | ✅ 已改为扁平路径 |
| B-02 | 顶层 `Makefile` 的 `report` 目标未处理多遍编译与错误中止 | 交叉引用/目录可能缺失；错误被吞 | ✅ 改用 `latexmk -xelatex -halt-on-error`，无 `latexmk` 时回退两遍 |
| B-03 | 论文正文含 emoji（🎯）与 `✓`，Latin Modern 无对应字形 | 编译警告、PDF 缺字 | ✅ 改为 `\checkmark` 并移除 emoji |
| B-04 | `create_test_initramfs.sh` 硬编码 6 个不存在的 `.c` 源文件 | 脚本在干净检出上必失败 | ✅ 改为扫描实际存在的 `*.c` 并逐个编译 |
| B-05 | `test_visualization/README.md` 描述了一个不存在的 Makefile | 用户按文档执行必然失败 | ✅ 改为手工命令，并补 `debug_scheduler.c` |
| B-06 | `CONTRIBUTING.md` 提到不存在的 `make simulator-linux` | 误导贡献者 | ✅ 改为 `make simulator-verify` / `make verify-all` |

### 2.2 文档一致性

| 编号 | 问题 | 状态 |
|------|------|------|
| D-01 | `docs/EXPERIMENT_REPORT.md` 有 8 条链接带 `:行号` 后缀，Markdown 无法解析 | ✅ 已剥离行号后缀 |
| D-02 | 两份实现文档互相引用时路径多写了一层 `../` | ✅ 已修正 |
| D-03 | `docs/ARCHITECTURE.md` 称 `simulator/Makefile` 仅支持 Windows、`lib/` 有 30+ JAR | ✅ 已更新为跨平台 + 单一依赖 |
| D-04 | `docs/DIRECTORY.md` 列出已删除的空文件、错误标注 Makefile 为 cmd 语法 | ✅ 已重写 |
| D-05 | `docs/DEVELOPMENT.md` 的 CRLF 警告与 `.gitattributes` 修复后的事实矛盾 | ✅ 已改为“已归一化 + 工具说明” |
| D-06 | `docs/archive/README.md` 称“路线未定”，但路线 A 早已决策 | ✅ 已按既定事实重写 |
| D-07 | `boot_test_scripts/README.md` 描述的文件全部不存在 | ✅ 已按实际文件重写 |
| D-08 | `yat_test_scripts/README.md` 硬编码 `~/LAB_ungraduate/...`，并引用不存在的测试程序 | ✅ 已改为相对路径 + 实际程序清单 |
| D-09 | `README.md` 依赖列表仍写 JGraphT / fastutil | ✅ 已改为仅 commons-math3 |
| D-10 | 缺少链接门禁，文档腐烂无法被 CI 发现 | ✅ 新增 [`scripts/check_docs.py`](../scripts/check_docs.py)，接入 Makefile 与 CI |

### 2.3 代码与设计（未处理，属后续工作）

| 编号 | 问题 | 位置 | 风险级别 |
|------|------|------|----------|
| K-01 | 全局就绪池只在 CPU0 上驱动调度决策 | [`balance_yat_casched`](../linux-6.8-main/kernel/sched/yat_casched.c) | 高（可扩展性瓶颈） |
| K-02 | 热路径中使用 `GFP_ATOMIC` 分配（`yat_calculate_recency` 每次 `kmalloc`） | `yat_casched.c` | 高（可能失败/抖动） |
| K-03 | 锁嵌套：`yat_pool_lock` → `global_dag_lock` → `dag->lock`，缺少统一锁序说明 | `enqueue_task` / `yat_update_ready_pool` | 高（死锁隐患） |
| K-04 | 复用 CFS 字段（`p->se.exec_start`、`prev_sum_exec_runtime`）传递自有语义 | `set_next_task` / `task_tick` | 中（与 CFS 语义耦合） |
| K-05 | 多个回调为空实现（`yield_task`、`set_cpus_allowed`、`rq_online`、`prio_changed` 等） | `DEFINE_SCHED_CLASS` | 中（行为不完整） |
| K-06 | `wakeup_preempt` 无条件 `resched_curr` | `wakeup_preempt_yat_casched` | 中（抢占过于激进） |
| K-07 | 热路径 `printk`（每次入队打印） | `yat_ajlr_enqueue_task` | 中（性能污染） |
| K-08 | `yat_get_crp_ratio` 为线性 stub，未实现真正的 CRP 衰减 | `yat_casched.c` | 中（与论文描述有差距） |
| K-09 | 用 `p->rt_priority` 承载作业优先级，语义借用 | `yat_ajlr_enqueue_task` | 中（可读性/可维护性） |
| K-10 | `SCHED_YAT_CASCHED = 8` 硬编码在 UAPI，存在与上游编号冲突的长期风险 | `include/uapi/linux/sched.h` | 低 |

### 2.4 模拟器方法学（未处理）

| 编号 | 问题 | 影响 |
|------|------|------|
| S-01 | CacheAware 的缓存收益内建于执行时间模型（`ET = expectedET·(1−save)`），WFD 无对应收益 | 收益方向被设计倾斜，不能直接当作“算法优越性”结论 |
| S-02 | WFD 命中率由公式估算并截断至 [0.30, 0.70]，与 CacheAware 的实测统计不同源 | 30pp 差值含模型假设成分 |
| S-03 | 指标分支依赖算法名字符串（`contains("CacheAware")` / `equals("Cache-Aware")`） | 重命名即静默改变指标 |
| S-04 | `CacheAware_v2` 使用静态可变状态，需显式 `resetState()` | 测试顺序敏感 |
| S-05 | 单一种子（42）、无置信区间与显著性检验 | 统计强度不足 |
| S-06 | 任务集为合成负载，`dagID=0`，无 DAG 依赖与通信开销 | 外部有效性受限 |
| S-07 | 使用 `javac`/`make` 直接编译，无 Maven/Gradle | 依赖与构建生态弱 |

### 2.5 仓库治理（部分处理）

| 编号 | 问题 | 状态 |
|------|------|------|
| R-01 | 整棵内核树入库（83k 文件、2 GB） | 未处理；见 DIRECTIONS 方向 A |
| R-02 | 大二进制入库（`proj59.pptx` 24 MB、多份 PDF） | 未处理；见 DIRECTIONS 方向 A |
| R-03 | `check_repo.sh` 空文件检查仅警告、不置失败，与脚本头部声明不一致 | 未处理（刻意保留宽松） |
| R-04 | CI 不编译内核、不编译论文 | 未处理；见 DIRECTIONS 方向 C |
| R-05 | 项目名多种写法并存 | 已通过 `docs/README.md` 与 `CONTRIBUTING.md` 明确约定；未批量重写 |

---

## 3. 当前门禁状态（本次整理后实测）

```text
make check        → exit 0（无冲突标记 / 无空文件 / 关键文件齐全）
make docs-check   → exit 0（23 个 Markdown，154 条链接，0 失效）
make symbol-check → exit 0（26 声明 / 26 非 static / 17 static）
make simulator-test  → 102/102 断言通过
make report       → exit 0（25 页 PDF，0 缺字警告）
```

CI 现包含：仓库检查、文档链接检查、符号一致性、冲突标记独立复核、模拟器编译 + 依赖自检 + 单测 + 快速回放。

---

## 4. 总体判断

| 维度 | 评价 |
|------|------|
| 算法/实现完整度 | 中上：AJLR 主链路可运行，但 CRP 与部分回调仍是 stub |
| 工程质量 | 中：门禁与文档体系已建立；内核热路径与锁序需专项审查 |
| 实验可信度 | 中下：结论来自解析模型，缺乏真实硬件与多 seed 验证 |
| 仓库可维护性 | 低：内核整树 + 大二进制导致克隆与评审成本过高 |
| 文档一致性 | 中上（整理后）：索引、链接、构建命令已对齐实际状态 |

**优先补齐的顺序**：仓库瘦身（A）→ 内核正确性/并发（B）→ 真实实验（C）→ 文档与流程细化（D）。
详见 [`DIRECTIONS.md`](DIRECTIONS.md)。
