# Yat-CAShed 模拟器实验报告

> 本文档记录 `simulator/` 中 **WFD vs CacheAware_v2** 对比实验的完整设计、原始结果、统计分析与有效性讨论，供论文/答辩引用与后续复现。
>
> 对应代码入口：[`AlgorithmComparisonExperiment.java`](../simulator/AlgorithmComparisonExperiment.java)、
> 指标计算：[`PerformanceAnalyzer.java`](../simulator/analyzer/PerformanceAnalyzer.java)、
> 可视化：[`visualize.py`](../simulator/visualizer/visualize.py)。
>
> 相关文档：[`ARCHITECTURE.md`](ARCHITECTURE.md) · [`DEVELOPMENT.md`](DEVELOPMENT.md) · [`ROADMAP.md`](ROADMAP.md)

---

## 1. 实验目的

1. 量化 **缓存感知调度（CacheAware_v2）** 相对 **经典负载均衡调度（WFD）** 在多核场景下的收益；
2. 验证收益在不同**系统利用率**（轻载 0.6 → 过载 1.5）下是否稳健；
3. 给出一套**固定种子、可复现**的实验基线，作为论文实验章节与后续算法迭代（`CacheAware_v3`、AJLR）的对照。

实验回答三个问题：

| 编号 | 问题 |
|------|------|
| Q1 | CacheAware_v2 是否降低 **Makespan**（总完工时间）？ |
| Q2 | 缓存命中率是否显著提升？提升幅度多少？ |
| Q3 | 收益是否以**牺牲负载均衡**为代价？在过载时是否退化？ |

---

## 2. 实验环境

| 项目 | 配置 |
|------|------|
| 操作系统 | WSL2，`Linux 6.6.87.2-microsoft-standard-WSL2` |
| 宿主 CPU | AMD Ryzen 9 7940H（12 逻辑核） |
| JDK | OpenJDK **21.0.12.1**（LTS），`javac` 目标版本 17 |
| Python | **3.12.15** |
| matplotlib | **3.11.2** |
| 实验运行参数 | `--cores=8`（模拟处理器数固定为 8，独立于宿主物理核数） |
| 依赖 | 仅 `lib/commons-math3-3.6.1.jar`（见 [`DEPENDENCIES.md`](DEPENDENCIES.md)） |

> 说明：模拟器为**离散事件驱动的解析模型**，运行时间不受宿主物理核数影响；`--cores=8` 是被模拟系统的处理器数量。

---

## 3. 实验设计

### 3.1 参数配置

| 参数 | 取值 | 说明 |
|------|------|------|
| 处理器数 `CORES` | **8** | 单处理器、同构 |
| 测试用例数 `TOTAL_TEST_CASES` | **100** | 每个利用率等级 20 例 |
| 每例任务数 `TASKS_PER_CASE` | **60** | 共 100 × 60 = 6000 个任务实例 |
| 随机种子 `SEED` | **42** | 固定，保证可复现 |
| 利用率等级 | **0.6 / 0.8 / 1.0 / 1.2 / 1.5** | 轻载 → 重度过载，各 20 例 |
| 高缓存敏感任务比例 | **0.7** | 70% 任务敏感度 ∈ [0.7, 0.95]，30% ∈ [0.1, 0.6] |

### 3.2 任务集生成

[`EnhancedTaskGenerator.generateCacheIntensiveTasks()`](../simulator/generator/EnhancedTaskGenerator.java:190)：

1. **利用率分布**：`UUnifastDiscard`（1000 次采样、丢弃失败样本）为 60 个任务分配单任务利用率 `u_i`，使 `Σu_i ≈ U`（目标系统利用率）。
2. **执行时间**：`expectedET = u_i × 1,000,000`（下限 10,000 时间单位）。
3. **缓存敏感度**：
   - 高敏感（70%）：`0.7 + rand × 0.25`
   - 低/中敏感（30%）：`0.1 + rand × 0.5`
4. **缓存权重** `weights[4]`（L1/L2/L3/Memory），高敏感任务更偏向 L1（0.6–0.9），低敏感任务分布更均匀；四者归一化。
5. **处理器亲和性**：60% 任务随机指定 `affinity ∈ [0, cores)`，否则 `-1`（无偏好）。

> 注：30% 的任务被标记为 `isCritical`，但本实验的对比指标不依赖该字段。

### 3.3 对比算法

| 算法 | 决策依据 | 执行时间来源 |
|------|---------|-------------|
| **WFD**（Baseline） | 选择当前累计负载**最小**的处理器（`processorLoads[proc] += expectedET`） | 原始 `expectedET`，**无缓存收益** |
| **CacheAware_v2** | 五维加权评分：缓存收益 40% + 负载均衡 30% + 缓存亲和性 20% + 缓存质量 10% − 缓存干扰惩罚 5% | 解析缓存收益模型（见下） |

**CacheAware_v2 评分函数**（[`calculateCacheAwareScore()`](../simulator/allocation/CacheAware_v2.java:90)）：

```
score = 0.40·Benefit + 0.30·LoadBalance + 0.20·Affinity + 0.10·Quality − 0.05·Interference
```

其中缓存收益（[`calculateCacheBenefitScore()`](../simulator/allocation/CacheAware_v2.java:130)）：

```
Benefit = (w1·s·0.6·(1−0.7·L1u) + w2·s·0.4·(1−0.5·L2u) + w3·s·0.3·(1−0.3·L3u)) · (1 + bonus)
bonus = 0.15 若 s>0.8；0.08 若 s>0.6；否则 0
```

**CacheAware 执行时间模型**（[`calculateExecutionTime()`](../simulator/allocation/CacheAware_v2.java:349)）：

```
save = (w1·(1−0.8·L1u)·s·0.25 + w2·(1−0.6·L2u)·s·0.15 + w3·(1−0.4·L3u)·s·0.08) · (1+bonus')
save = min(save, 0.50)
ET   = max(expectedET·(1−save), expectedET/2)
```

**CRP（缓存驻留预测）** 在模拟器中由 [`RecencyProfileReal.computeET()`](../simulator/entity/RecencyProfileReal.java:54) 实现，供一致性校验使用：L1 命中 ×0.8、L2 命中 ×0.9（历史按 `processor/2` 分桶）、L3 命中 ×0.95、内存访问 ×1.2、故障 ×1.5，命中窗口为最近 5 次同 DAG 访问。其边界行为已由单元测试覆盖（见第 6 节）。

### 3.4 指标定义

| 指标 | 定义（实现位置） |
|------|----------------|
| **Makespan** | 所有处理器中最晚完成时间（[`calculateMakespan`](../simulator/analyzer/PerformanceAnalyzer.java:160)） |
| **CacheHit** | CacheAware：反射调用 `CacheAware_v2.calculateCacheHitRatioFromTasks()` 取实际统计；WFD：按权重公式估算并截断至 [0.30, 0.70]（[`calculateWFDCacheHitRatio`](../simulator/analyzer/PerformanceAnalyzer.java:316)） |
| **TotalExecutionTime** | `Σ(finishAt − start)`（[`calculateTotalTaskExecutionTime`](../simulator/analyzer/PerformanceAnalyzer.java:436)） |
| **AvgExecutionTime** | 上一项 / 任务数 |
| **ResponseTime** | `Σ(finishAt − release) / 有效任务数` |
| **CpuUtilization** | `ΣET / (Makespan × cores)` |
| **LoadBalance** | `1 − std(每处理器负载)/avg(负载)`，越接近 1 越均衡 |

### 3.5 统计口径

报告同时给出两种口径，二者含义不同，**不可混用**：

- **平均每例相对改进（paired）**：对每个用例 `i` 计算 `(M_wfd,i − M_ca,i)/M_wfd,i`，再对 100 例取算术平均 → **11.66%**。反映"典型用例上的改进幅度"。
- **总均值之比**：`(mean(M_wfd) − mean(M_ca)) / mean(M_wfd)` → **11.92%**。等价于终端"性能改进总结"输出的数值。

结论方向一致，差异仅来自加权方式。

---

## 4. 实验结果

### 4.1 总体结果（100 用例，每例 60 任务）

| 指标 | CacheAware_v2 | WFD | 差异 |
|------|--------------|-----|------|
| Makespan 均值 | **155,058.5** | 176,043.8 | **−11.92%**（逐例平均 −11.66%） |
| Makespan 标准差 | 39,614.6 | 44,777.1 | 波动更小（−11.5%） |
| Makespan 范围 | [95,470, 262,340] | [109,184, 281,443] | 上限低 19,103 |
| **缓存命中率** | **73.81%** | 43.61% | **+30.21 个百分点** |
| 总执行时间 | 997,741.4 | 1,160,520.0 | **−14.03%** |
| 平均任务执行时间 | 16,629.0 | 19,342.0 | −14.03% |
| 平均响应时间 | 67,605.6 | 79,346.9 | **−14.80%** |
| CPU 利用率 | 0.8111 | 0.8315 | −0.0204 |
| 负载均衡度 | 0.8627 | 0.8747 | −0.0120 |

**胜负统计（按 Makespan）**：CacheAware_v2 胜 **96/100**，WFD 胜 4/100。

逐例相对改进分布：均值 **11.66%**、中位数 **12.54%**、最小 **−3.37%**、最大 **+24.45%**。
缓存命中率：**100/100** 用例提升；总执行时间：**100/100** 用例改进。

### 4.2 按利用率等级分解

| 利用率 | CA Makespan | WFD Makespan | Makespan 改进 | CA 命中率 | WFD 命中率 | 差值 (pp) | 总执行时间改进 | 胜场 |
|-------|------------|-------------|--------------|----------|-----------|----------|--------------|------|
| 0.6 | 107,418 | 119,264 | **9.69%** | 73.72% | 43.58% | +30.14 | 14.05% | 20/20 |
| 0.8 | 125,023 | 143,594 | **12.64%** | 73.78% | 43.66% | +30.12 | 13.95% | 20/20 |
| 1.0 | 157,158 | 180,021 | **12.61%** | 73.91% | 43.57% | +30.34 | 13.72% | 18/20 |
| 1.2 | 172,961 | 198,480 | **12.69%** | 73.88% | 43.62% | +30.25 | 14.09% | 20/20 |
| 1.5 | 212,733 | 238,859 | **10.65%** | 73.78% | 43.60% | +30.18 | 14.22% | 18/20 |

观察：

1. **中等负载（0.8–1.2）收益最高**（≈12.6%），此时缓存复用与负载均衡可同时兼顾；
2. **轻载（0.6）收益下降**至 9.69%——处理器本就空闲，缓存收益对关键路径的贡献被摊薄；
3. **重度过载（1.5）收益下降**至 10.65%，且出现 2 例失败——负载压力大时，缓存亲和性与均衡目标冲突加剧；
4. 缓存命中率提升在**所有负载下稳定在 30.1–30.3pp**，说明该收益主要由"是否感知缓存"决定，与系统压力弱相关。

### 4.3 失败案例分析（4/100）

| 用例 | 利用率 | CA Makespan | WFD Makespan | 相对劣化 |
|------|--------|------------|-------------|---------|
| 52 | 1.0 | 194,392 | 193,573 | −0.42% |
| 57 | 1.0 | 157,408 | 152,278 | **−3.37%** |
| 88 | 1.5 | 220,765 | 214,760 | −2.80% |
| 95 | 1.5 | 226,682 | 226,276 | −0.18% |

共性：

- 全部出现在 **U ≥ 1.0**，即满载/过载区；
- 4 例中有 2 例劣化 < 0.5%（近似持平），仅 **case 57 的 −3.37%** 是实质性退化；
- 推测原因：CacheAware_v2 的评分中缓存收益权重（40%）高于负载均衡（30%），在极端负载下会把任务"钉"在高缓存亲和但已较忙的处理器上，导致尾部长任务拖高 Makespan。这与 4.1 中 LoadBalance（0.8627 < 0.8747）略逊于 WFD 的结论一致。

### 4.4 收益与代价的取舍

| 维度 | CacheAware_v2 | WFD | 结论 |
|------|--------------|-----|------|
| Makespan | 更优（−11.92%） | — | ✅ 主要目标达成 |
| 缓存命中率 | 73.81% | 43.61% | ✅ 显著提升 |
| 响应时间 | 更优（−14.80%） | — | ✅ |
| CPU 利用率 | 0.8111 | 0.8315 | ⚠️ 略低（缓存收益降低单任务耗时，同 Makespan 下有效作业量占比下降，属模型口径效应） |
| 负载均衡 | 0.8627 | 0.8747 | ⚠️ 略差（可解释 4 例失败） |

---

## 5. 图表

可视化脚本 [`visualize.py`](../simulator/visualizer/visualize.py) 从 CSV 生成 3 张 300 dpi 图（输出到 `simulator/result/`，该目录已在 `.gitignore` 中，需本地运行生成）：

| 文件 | 内容 |
|------|------|
| `makespan_analysis.png` | Makespan 分布（箱线图/散点）+ 逐用例对比 + 改进幅度 |
| `cache_hit_analysis.png` | 缓存命中率分布与逐用例对比 |
| `execution_time_and_win_rate_analysis.png` | 执行时间对比 + 胜率饼图/柱状图 |

数据源：`simulator/result/algorithm_comparison_results.csv`（200 行 = 2 算法 × 100 用例）。

**本次运行期间修复的两个可视化缺陷**（此前会导致图表静默缺失）：

1. `runPythonVisualization()` 在 `python` 失败、`python3` 成功后仍记录为 `python`，导致子进程启动失败而无图表；
2. matplotlib ≥ 3.9 移除了 `Axes.boxplot(labels=...)`，在 3.11 上抛 `TypeError`；已改为 `set_xticks` + `set_xticklabels` 兼容写法。

---

## 6. 正确性验证

实验结论的可信度依赖底层模型的正确性，测试覆盖情况：

| 测试项 | 断言数 | 说明 |
|--------|-------|------|
| `[1]` Node 基础属性 | 6 | 构造、DAG ID、执行时间 |
| `[2]` CRP 执行时间 | 12 | L1/L2/L3/内存、故障放大 |
| `[2b]` **CRP 边界** | 14 | 同级同时命中取 L1、跨 DAG 不命中、5 次访问窗口边界、故障叠加、`(long)` 截断（base=1）、base=0 |
| `[3]` WFD | 6 | 最轻负载选择、负载累加、重置 |
| `[4]` CacheAware | 6 | 评分与分配有效性 |
| `[5]` **Speedup/Benefit 边界** | 14 | 空集、敏感度阈值 0.5、命名匹配（`Cache-Aware` vs `CacheAware`）、Makespan/负载均衡/CPU 利用率公式、顺序无关性 |
| **合计** | **58** | `make -C simulator test` 全部通过 |

配套门禁（本次均通过）：

```bash
make check            # 仓库健康检查 → exit 0
make symbol-check     # 内核符号一致性 → exit 0
make simulator-test   # 58/58 断言通过
make simulator-verify # 编译 + 依赖自检 + 单测 + 快速实验 → exit 0
```

---

## 7. 有效性威胁与局限

本报告的结果应当在这些前提下解读，**避免过度声称**：

1. **缓存命中率非硬件实测。**
   CacheAware 的命中率取自算法内部的解析统计（`actualL1L2L3HitRatio`），WFD 的命中率由固定公式估算并**截断在 [30%, 70%]**。因此 **30.21pp 的差距中包含模型假设的成分**，不等于真实 CPU 上 `perf` 测得的差异。若要作为硬件结论，需在内核调度器 + 真实负载下复测。

2. **执行时间收益部分内建于模型。**
   CacheAware 使用解析式 `ET = expectedET·(1−save)`（`save ≤ 0.5`），而 WFD 直接使用 `expectedET`。优势方向在设计上已被倾斜，故 Makespan 改进应理解为"**在该模型假设下**的收益"。

3. **命名匹配脆弱。**
   `PerformanceAnalyzer.calculateCacheHitRatio()` 用 `algorithmName.contains("CacheAware")` 分支，而 `calculateCacheSensitivityBenefit()` 用精确 `"Cache-Aware".equals(...)`。历史版本曾因命名不一致（`CacheAware_v2` vs `Cache-Aware`）导致某指标恒为 0；`[5]` 边界测试已把该行为**固化断言**，后续重命名须同步更新。

4. **单一种子、样本量有限。**
   仅 `--seed=42`、100 例。未提供置信区间与显著性检验。建议后续以 10 个种子重复并报告均值 ± 95% CI，或至少做配对 t 检验 / Wilcoxon 符号秩检验。

5. **合成负载。**
   任务集由 `UUnifastDiscard` + 参数化敏感度生成，未使用真实应用 trace（如 PARSEC/SPEC）。结论的外部有效性受限于此。

6. **未刻画 DAG 依赖。**
   `Node.dagID = 0`（缓存密集型任务）且分配循环按任务顺序单步推进，未模拟真实 DAG 前驱约束与通信开销。

7. **负载均衡与 CPU 利用率略逊。**
   这两项不是主指标，但应在论文中**如实报告**，并作为 `v3` 的改进方向（如动态调整缓存收益权重）。

---

## 8. 复现步骤

```bash
# 1) 全量实验（100 用例 + 可视化，默认 seed=42, cores=8, tasks=60）
make -C simulator run

# 2) 仅跑实验、跳过 Python 图表（CI/快速验证）
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment --no-viz

# 3) 自定义规模
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment \
     --cores=8 --cases=200 --tasks=80 --seed=2024

# 4) 单元测试与门禁
make -C simulator test
make check && make symbol-check
```

产物：

- `simulator/result/algorithm_comparison_results.csv`（200 行原始数据）
- `simulator/result/*.png`（3 张图表，300 dpi）

> **可复现性**：固定 `seed=42`、固定参数下输出确定；CSV 与图表可逐次比对。

---

## 9. 结论

1. 在 8 核、60 任务、5 档利用率、100 用例的合成负载下，**CacheAware_v2 相比 WFD 平均降低 Makespan 11.66%（逐例）/ 11.92%（均值之比），96% 用例取胜**，方向与量级稳定。
2. **缓存命中率由 43.61% 提升至 73.81%（+30.21pp）**，在全部 100 例上一致为正，且几乎不随利用率变化。
3. 收益在**中等负载（0.8–1.2）最大**；轻载与被重度过载两端均有所衰减。
4. 代价是**负载均衡度与 CPU 利用率小幅下降**，并在过载区产生 4 例劣化（最差 −3.37%）。这为下一版算法指明了具体调优方向：**在 U ≥ 1.0 时自适应下调缓存收益权重（当前 0.40）**。
5. 上述数值均来自**解析模型**，其中缓存命中率与执行时间收益含模型内建假设；作为硬件级结论前，需在内核调度器与真实 trace 上复测。

---

*最后更新：全量 100 用例实验（JDK 21 / Python 3.12 / matplotlib 3.11），58/58 单测通过。*
