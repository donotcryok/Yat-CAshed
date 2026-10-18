# Yat-CAShed 模拟器实验报告

> 本文档记录 `simulator/` 中 **WFD vs CacheAware_v2** 对比实验的完整设计、原始结果、统计分析与有效性讨论，供论文/答辩引用与后续复现。
>
> **⚠️ 重要版本说明（公平基线 C1，本文默认口径）**
> 2026 年对基线做了公平性审计与改造：此前的对比中，WFD 与 CacheAware **并非在同一个执行时间/缓存命中模型下评估**，
> CacheAware 同时享受"自估执行时间收益"与"自报命中率"两项内建优势。
> 改造后两种算法**只允许在"选哪个处理器"上不同**，执行时间与命中率统一由
> [`SharedCacheModel`](../simulator/entity/SharedCacheModel.java) 计算。
> 改造后结论发生实质变化：**CacheAware_v2 在 Makespan 上不再具有统计显著优势**（见 §3.6 与 §4）。
> 历史（旧口径）数据仍可通过 `--legacy-metrics` 逐位复现，仅用于对照与审计，**不应再作为算法有效性证据**。
>
> 对应代码入口：[`AlgorithmComparisonExperiment.java`](../simulator/AlgorithmComparisonExperiment.java)、
> 共享模型：[`SharedCacheModel.java`](../simulator/entity/SharedCacheModel.java)、
> 指标计算：[`PerformanceAnalyzer.java`](../simulator/analyzer/PerformanceAnalyzer.java)、
> 可视化：[`visualize.py`](../simulator/visualizer/visualize.py)。
>
> 相关文档：[`ARCHITECTURE.md`](ARCHITECTURE.md) · [`DEVELOPMENT.md`](DEVELOPMENT.md) · [`ROADMAP.md`](ROADMAP.md)

---

## 1. 实验目的

1. 量化 **缓存感知调度（CacheAware_v2）** 相对 **经典负载均衡调度（WFD）** 在多核场景下的收益；
2. 验证收益在不同**系统利用率**（轻载 0.6 → 过载 1.5）下是否稳健；
3. 给出一套**固定种子、可复现**的实验基线，作为论文实验章节与后续算法迭代（`CacheAware_v3`、AJLR）的对照。

实验回答四个问题：

| 编号 | 问题 |
|------|------|
| Q1 | CacheAware_v2 是否降低 **Makespan**（总完工时间）？ |
| Q2 | 缓存命中率是否显著提升？提升幅度多少？ |
| Q3 | 收益是否以**牺牲负载均衡**为代价？在过载时是否退化？ |
| Q4 | 上述收益在**两算法共用同一执行/缓存模型**（公平基线 C1）后是否依然成立？ |

> 本报告的核心结论以 **Q4** 为前提：只有 Q4 成立，Q1–Q3 才具有算法层面的意义。

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

[`EnhancedTaskGenerator.generateCacheIntensiveTasks()`](../simulator/generator/EnhancedTaskGenerator.java)：

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
| **WFD**（Baseline） | 选择当前累计负载**最小**的处理器（`Map<Integer,Long>` 动态负载表，`+= expectedET`） | 与 CacheAware **同一** `SharedCacheModel.execute()` |
| **CacheAware_v2** | 五维加权评分：缓存收益 40% + 负载均衡 30% + 缓存亲和性 20% + 缓存质量 10% − 缓存干扰惩罚 5% | 与 WFD **同一** `SharedCacheModel.execute()` |

> **公平性约束（C1 起强制）**：两种算法共用同一次任务生成的**深拷贝**、同一套
> [`SharedCacheModel`](../simulator/entity/SharedCacheModel.java) 实例与同样的调用顺序；
> 唯一的差异是 `allocate()` 返回的处理器编号。`task.expectedET` 在整个仿真过程中**不可变**。

**CacheAware_v2 评分函数**（[`calculateCacheAwareScore()`](../simulator/allocation/CacheAware_v2.java)）：

```
score = 0.40·Benefit + 0.30·LoadBalance + 0.20·Affinity + 0.10·Quality − 0.05·Interference
```

其中缓存收益（[`calculateCacheBenefitScore()`](../simulator/allocation/CacheAware_v2.java)）：

```
Benefit = (w1·s·0.6·(1−0.7·L1u) + w2·s·0.4·(1−0.5·L2u) + w3·s·0.3·(1−0.3·L3u)) · (1 + bonus)
bonus = 0.15 若 s>0.8；0.08 若 s>0.6；否则 0
```

**执行时间模型（C1 起，两种算法共用）** —— [`SharedCacheModel.execute()`](../simulator/entity/SharedCacheModel.java)：

```
level  = resolveCacheLevel(task, processor)   // 1=L1, 2=L2, 3=L3, 4=内存
ET     = expectedET × factor(level)            // L1 0.8 / L2 0.9 / L3 0.95 / 内存 1.2
ET     = ET × 1.5                              // 若 task.hasFaults
ET     = max(ET, expectedET / 2)               // 下界，避免负收益
ET     = max(ET, 1)                            // 非零下界
```

其中 `level` 由 CPU 私有历史（每处理器、每缓存层级各保留最近 **5** 条）判定：若该处理器最近访问过**同 `dagID`** 的任务，则命中对应层级，否则退化为内存访问。

> 旧口径中 CacheAware 使用的解析式 `ET = expectedET·(1−save)`（`save ≤ 0.5`）**仅保留在
> [`CacheAware_v2.calculateExecutionTime()`](../simulator/allocation/CacheAware_v2.java) 内**，
> 只在 `--legacy-metrics` 下被调用，用于逐位复现历史结果。

**CRP（缓存驻留预测）** 在模拟器中由 [`RecencyProfileReal.computeET()`](../simulator/entity/RecencyProfileReal.java) 实现，与 `SharedCacheModel` 采用**完全相同的层级系数与命中窗口**：L1 命中 ×0.8、L2 命中 ×0.9（历史按 `processor/2` 分桶）、L3 命中 ×0.95、内存访问 ×1.2、故障 ×1.5，命中窗口为最近 5 次同 DAG 访问。其边界行为已由单元测试覆盖（见第 6 节）。

### 3.4 指标定义

| 指标 | 定义（实现位置） |
|------|----------------|
| **Makespan** | 所有处理器中最晚完成时间（[`calculateMakespan`](../simulator/analyzer/PerformanceAnalyzer.java)） |
| **CacheHit** | **算法无关**：优先取任务上由 `SharedCacheModel` 写入的 `actualCacheHitRatio`（[`calculateMeasuredCacheHitRatio`](../simulator/analyzer/PerformanceAnalyzer.java)）；无实测值时退回基础估算。旧口径的名字分派估算器（WFD 截断至 [0.30, 0.70]、CacheAware 反射取自估值）**仅在 `--legacy-metrics` 下可达** |
| **TotalExecutionTime** | `Σ(finishAt − start)`（[`calculateTotalTaskExecutionTime`](../simulator/analyzer/PerformanceAnalyzer.java)） |
| **AvgExecutionTime** | 上一项 / 任务数 |
| **ResponseTime** | `Σ(finishAt − release) / 有效任务数` |
| **CpuUtilization** | `ΣET / (Makespan × cores)` |
| **LoadBalance** | `1 − std(每处理器负载)/avg(负载)`，越接近 1 越均衡 |

**命中率定义**（`SharedCacheModel.weightedHitRatio`）：给定层级 `level` 与归一化权重 `w[0..3]`（L1/L2/L3/内存），

```
L1 命中: hits = 3·w1 + 2·w2 + 1·w3
L2 命中: hits = 1·w1 + 2·w2 + 1·w3
L3 命中: hits = 0·w1 + 0·w2 + 1·w3
内存   : hits = 0
ratio = hits / (w1+w2+w3+w4)
```

即"层级命中"按访问权重折算为"命中份额"，内存访问不计入分子。该口径对两种算法**完全一致**，不含任何按算法名分派的常数。

### 3.5 统计口径

**单种子（seed=42，100 用例）**给出两种聚合方式，含义不同，**不可混用**：

- **平均每例相对改进（paired）**：对每个用例 `i` 计算 `(M_wfd,i − M_ca,i)/M_wfd,i`，再对 100 例取算术平均。反映"典型用例上的改进幅度"，也是终端"性能改进总结"输出的数值。
- **总均值之比**：`(mean(M_wfd) − mean(M_ca)) / mean(M_wfd)`。对大 Makespan 用例加权更重。

**多种子（`--seeds=42,43,44,45,46`，n=5）**额外报告：样本均值、样本标准差（n−1 分母）、以及
`95% CI = t(0.975, n−1) · sd / √n`。**判断算法是否具有优势，以 95% CI 是否跨 0 为准**，而非单个种子的点估计。

> **临界值必须用 t 分布而非 1.96**：n=5 时 `t(0.975,4)=2.776`，是 1.96 的 1.42 倍。
> 早期版本用正态近似，把区间低估约 29%，会把「不显著」误报成「显著」。
> 已更正（见 [`CHANGELOG.md`](../CHANGELOG.md)），代码实现为
> [`AlgorithmComparisonExperiment.tCritical95()`](../simulator/AlgorithmComparisonExperiment.java#L253)。

### 3.6 公平基线改造（C1）——改造了什么、为什么

改造前审计出 **4 处**系统性偏向 CacheAware 的实现缺陷：

| # | 缺陷 | 影响 |
|---|------|------|
| 1 | `simulateCacheAwareAllocation` 把自估 ET **写回** `task.expectedET`，而 WFD 使用原始 ET | CacheAware 凭空获得最高 50% 的执行时间减免 |
| 2 | `PerformanceAnalyzer` 按**算法名**分派命中率估算器：WFD 被硬截断在 [0.30, 0.70]，CacheAware 读取自己上报的统计 | 命中率差距部分由**度量方式**制造，而非调度行为 |
| 3 | `WFD` 的负载表硬编码 `new int[8]` | 核数 ≠ 8 时越界或统计失真 |
| 4 | `createCacheIntensiveTask()` 硬编码 `dagID = 0` | 所有任务同属一个 DAG，"最近访问"判定退化为纯窗口计数，缓存局部性信号被抹平 |

改造内容：

- 新增 [`SharedCacheModel`](../simulator/entity/SharedCacheModel.java)：**唯一**的执行时间与命中率来源，两种算法共用；`expectedET` 全程不可变；
- `PerformanceAnalyzer` 默认优先使用实测字段，方法签名改为算法无关；
- 旧估算器 + 旧写回逻辑统一收敛到 `--legacy-metrics` 开关（`PerformanceAnalyzer(boolean)`、`CacheAware_v2.setLegacyWriteBack()`）；
- `WFD` 改用动态 `Map<Integer, Long>` 负载表；
- [`EnhancedTaskGenerator.DAG_TASKS_PER_GROUP = 5`](../simulator/generator/EnhancedTaskGenerator.java)：`dagID = taskId / 5`，恢复 DAG 局部性；
- 新增 `--seeds=` 多种子聚合。

**改造前后对照（同一 seed=42、同一 100 用例）**：

| 指标 | 旧口径 `--legacy-metrics` | 公平口径（默认） | 说明 |
|------|--------------------------|-----------------|------|
| 平均 Makespan 改进（paired） | **11.66%** | **0.39%** | 缩水约 30 倍 |
| CacheAware 平均 Makespan | 155,059 ns | 176,437 ns | — |
| WFD 平均 Makespan | 176,044 ns | 177,686 ns | — |
| 缓存命中率提升 | **+30.21 pp** | **+0.82 pp** | 缩水约 37 倍 |
| CacheAware 命中率 | 73.8% | 18.9% | 旧值为算法自报，非实测 |
| WFD 命中率 | 43.6% | 18.1% | 旧值为公式估算并截断 |
| CacheAware 胜率 | **96.0%** | **56.0%** | 从"压倒性"变为"抛硬币" |

> 旧口径数值可逐位复现：`java ... AlgorithmComparisonExperiment --no-viz --legacy-metrics`。
> 这是**审计工具**，不是算法结论。

---

## 4. 实验结果

### 4.1 总体结果（公平基线，100 用例，每例 60 任务）

| 指标 | CacheAware_v2 | WFD | 差异 |
|------|--------------|-----|------|
| Makespan 均值 | 176,436.8 | 177,686.2 | **−0.70%**（逐例平均 **−0.39%**） |
| Makespan 标准差 | 46,735.1 | 47,020.4 | 基本持平（−0.6%） |
| **缓存命中率** | **18.90%** | 18.08% | **+0.82 个百分点** |
| 总执行时间 | 1,143,921.0 | 1,144,965.6 | −0.09% |
| 平均任务执行时间 | 19,065.4 | 19,082.8 | −0.09% |
| 平均响应时间 | 79,662.9 | 78,511.8 | **+1.47%（更差）** |
| CPU 利用率 | 0.8195 | 0.8157 | +0.0038 |
| 负载均衡度 | 0.8692 | 0.8650 | +0.0041 |

> 终端输出的 "Makespan 减少 1249 ns (0.70%)" 为**均值之比**；"平均 Makespan 改进 0.39%" 为**逐例配对平均**。两种口径在公平基线下方向一致但都非常小。

**胜负统计（按 Makespan）**：CacheAware_v2 胜 **56/100**，WFD 胜 **44/100**（无平局）。

逐例相对改进分布：均值 **0.39%**、中位数 **1.34%**、最小 **−20.98%**、最大 **+16.49%**、
标准差 **7.70%**、**95% CI = ±1.51%**。
缓存命中率：**59/100** 用例提升（旧口径为 100/100），差值分布均值 +0.82 pp、标准差 2.69 pp、95% CI = ±0.53 pp。

**多种子验证（`--seeds=42,43,44,45,46`，n=5）**：

| 指标 | 均值 | 标准差 | 95% CI（t=2.776） | 样本 |
|------|------|--------|--------|------|
| Makespan 改进 (%) | **−0.954** | 1.406 | ±1.745 | [−1.54, −1.58, +1.53, −1.91, −1.27] |
| 命中率提升 (pp) | **+0.377** | 0.272 | ±0.338 | [+0.63, +0.35, −0.03, +0.30, +0.63] |
| CacheAware 胜率 (%) | **48.6** | 5.771 | ±7.164 | [46, 43, 57, 45, 52] |

**判定**：

- Makespan 改进的 95% CI（[−2.70, +0.79]）**跨 0**，且点估计为**负值**（CacheAware 平均更慢）；
- 胜率 48.6% ± 7.16%，**与 50% 不可区分**；
- 命中率提升 +0.38 pp（CI **[+0.04, +0.72]**，刚好不含 0），量级不足 0.5 个百分点 ——
  即便按 t 分布放宽后仍为正，但远不足以支撑任何"性能优势"表述；
- **结论：在公平基线下，CacheAware_v2 相对 WFD 没有可观测的 Makespan 优势。**

> 本节早期版本用 `1.96·sd/√n` 得到 ±1.233 / ±0.238 / ±5.058。n=5 应取 `t(0.975,4)=2.776`，
> 已更正为 ±1.745 / ±0.338 / ±7.164。**结论方向不变**，但原来「命中率 CI 明显不含 0」
> 的表述需弱化为「勉强不含 0」。

### 4.2 按利用率等级分解

| 利用率 | CA Makespan | WFD Makespan | Makespan 改进 | CA 命中率 | WFD 命中率 | 差值 (pp) | 总执行时间改进 | 胜场 |
|-------|------------|-------------|--------------|----------|-----------|----------|--------------|------|
| 0.6 | 120,272 | 119,381 | **−0.90%** | 18.40% | 15.58% | +2.82 | +0.51% | 9/20 |
| 0.8 | 142,028 | 145,694 | **+2.11%** | 18.43% | 16.55% | +1.89 | +0.41% | 13/20 |
| 1.0 | 180,296 | 182,153 | **+0.74%** | 18.64% | 18.02% | +0.63 | −0.00% | 13/20 |
| 1.2 | 198,278 | 200,960 | **+0.79%** | 18.84% | 19.16% | −0.32 | −0.06% | 11/20 |
| 1.5 | 241,310 | 240,243 | **−0.81%** | 20.18% | 21.08% | −0.91 | −0.13% | 10/20 |

观察：

1. **改进幅度全部落在 ±2.2% 以内**，且方向随负载翻转：轻载（0.6）与重载（1.5）为负，中载（0.8–1.2）为正；
2. 这一"中间正、两头负"的形状与"缓存复用收益 vs. 负载均衡损失"的权衡一致，但幅度已经小到接近噪声；
3. **命中率差随负载单调下降**（+2.82 → −0.91 pp）：负载较高时，WFD 的均匀铺开反而让同一 DAG 的任务更集中，命中率反超 CacheAware；
4. 旧口径下"命中率稳定 +30pp、与负载弱相关"的结论**不再成立**。

### 4.3 失败/大偏差案例分析

公平基线下不再有"系统性失败"，取而代之的是**双向的大幅偏差**：100 例中 **60 例 |Δ| > 5%**、**21 例 |Δ| > 10%**。

| 用例 | 利用率 | CA Makespan | WFD Makespan | Δ（CA 视角） |
|------|--------|------------|-------------|-------------|
| 95 | 1.5 | 265,315 | 219,313 | **−20.98%**（最差） |
| 88 | 1.5 | 254,405 | 221,591 | −14.81% |
| 69 | 1.2 | 212,064 | 184,938 | −14.67% |
| 33 | 0.8 | 149,351 | 131,690 | −13.41% |
| 19 | 0.6 | 139,758 | 125,076 | −11.74% |
| … | | | | |
| 55 | 1.0 | 182,288 | 207,726 | +12.25% |
| 9 | 0.6 | 115,088 | 132,110 | +12.88% |
| 78 | 1.2 | 211,253 | 246,890 | +14.43% |
| 61 | 1.2 | 190,264 | 222,453 | +14.47% |
| 80 | 1.2 | 181,885 | 217,788 | **+16.49%**（最好） |

- 逐例标准差 **7.70%** 是均值（0.39%）的 **20 倍**，说明**结果由个别用例的随机结构决定**，而非算法稳定收益；
- 双向极值同时存在且大致对称，进一步支持"两算法在公平模型下等效"的判断；
- 旧口径下"最差仅 −3.37%"的窄分布是**模型内建优势压缩了方差**的产物，不代表算法更稳。

### 4.4 收益与代价的取舍

| 维度 | CacheAware_v2 | WFD | 结论 |
|------|--------------|-----|------|
| Makespan | 176,437 | 177,686 | ➖ 差异不显著（CI 跨 0） |
| 缓存命中率 | 18.90% | 18.08% | ➖ +0.82 pp，量级过小 |
| 响应时间 | 79,663 | 78,512 | ❌ **比 WFD 差 1.47%** |
| CPU 利用率 | 0.8195 | 0.8157 | ➖ 基本持平 |
| 负载均衡 | 0.8692 | 0.8650 | ➖ 基本持平（略优） |

**核心取舍结论**：在统一模型下，CacheAware_v2 用**更复杂的五维评分**换取的收益无法被测量到，
且付出了响应时间劣化 1.47% 的代价。这说明当前评分权重（缓存收益 0.40 / 负载 0.30）在
**8 核、60 任务、窗口 5** 的设定下**不是有效配置**。

### 4.5 为什么收益消失？——机制层面的解释

用诊断探针对单次运行做分解（8 核、60 任务、U=1.0、seed=42）：

| 指标 | WFD | CacheAware_v2 |
|------|-----|--------------|
| 层级分布 [L1, L2, L3, 内存] | [0, 10, 38, 12] | [2, 6, 40, 12] |
| 平均实测命中率 | 0.1891 | 0.2040 |
| `ΣET / ΣexpectedET` | 0.9845 | 0.9861 |
| 同核相邻同 DAG 次数 | **1** | **6** |

解读：

1. CacheAware **确实**把更多任务放到了有局部性的核上（6 vs 1），这是算法在起作用；
2. 但缓存模型的最大收益只有 L1 的 ×0.8，而 L1 命中在 60 个任务中仅 **2 次**；
   L3 命中（×0.95）占绝大多数（40/60），**收益空间本身极小**；
3. 因此省下的执行时间被"把这个任务放到更忙的核"造成的排队延迟抵消甚至反超；
4. 旧口径下这一权衡被掩盖，因为 CacheAware 的 ET 由**自估模型**给出（最高减免 50%），并非由放置决策产生。

> **对研究路线的含义**：若要让缓存感知调度真正体现价值，需要
> (a) 增大层级系数差（L1 与内存的差距）、(b) 让窗口/局部性结构更贴近真实 trace、
> 或 (c) 在评分中引入"局部性收益是否落在关键路径上"的判断。
> 单纯调整权重无法突破当前模型给出的收益上界。

---

## 5. 图表

可视化脚本 [`visualize.py`](../simulator/visualizer/visualize.py) 从 CSV 生成 3 张 300 dpi 图（输出到 `simulator/result/`，该目录已在 `.gitignore` 中，需本地运行生成）：

| 文件 | 内容 |
|------|------|
| `makespan_analysis.png` | Makespan 分布（箱线图/散点）+ 逐用例对比 + 改进幅度 |
| `cache_hit_analysis.png` | 缓存命中率分布与逐用例对比 |
| `execution_time_and_win_rate_analysis.png` | 执行时间对比 + 胜率饼图/柱状图 |

数据源：`simulator/result/algorithm_comparison_results.csv`（200 行 = 2 算法 × 100 用例）。

> **公平基线下的图表读法**：图表已经反映 C1 口径（命中率约 18%，胜率饼图接近 50/50），
> 与旧文档中的截图（命中率 73.81%、96% 胜率）**不同**。若需旧图对照，请用
> `--legacy-metrics` 重跑生成，并明确标注为"历史口径"。

**此前修复的两个可视化缺陷**（会导致图表静默缺失）：

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
| `[6]` **公平基线 C1** | **14** | `SharedCacheModel` 不修改 `expectedET`；冷启动 → 内存层且 `ET = base×1.2`；同 DAG 同核第二次 → L1 且 `ET = base×0.8`；L1 命中时 `actualL1L2L3HitRatio` 三项和为 3.0；同一放置序列下两算法 `ΣET` 相等；实测命中率与算法名无关；legacy 开关默认关闭且可显式开启；公平口径下 `actualCacheHitRatio` 初始为 0 |
| **合计** | **72** | `make -C simulator test` 全部通过 |

配套门禁：

```bash
make check            # 仓库健康检查 → exit 0
make symbol-check     # 内核符号一致性 → exit 0
make simulator-test   # 72/72 断言通过
make simulator-verify # 编译 + 依赖自检 + 单测 + 快速实验 → exit 0
make verify-all       # 以上全部 + 文档/补丁检查
```

---

## 7. 有效性威胁与局限

本报告的结果应当在这些前提下解读，**避免过度声称**：

1. **命中率是模型量，不是硬件量。**
   C1 之后两种算法的命中率都由 `SharedCacheModel` 用同一套层级系数计算，**口径上已公平**，
   但层级系数（L1 0.8 / L2 0.9 / L3 0.95 / 内存 1.2）本身是**假设值**，未经真实 CPU 的
   `perf stat -e cache-misses` 标定。18.9% 这个绝对值**不可外推**到真实系统。

2. **收益上界由模型决定，而非由算法决定。**
   在窗口 5、L3 主导（40/60 任务）的设定下，即使 CacheAware 完美命中，可节省的执行时间
   也只有个位数百分比。**当前实验测量的是"该模型下的收益"，不是"缓存感知调度的理论上限"。**
   若论文需要展示收益，应先论证模型参数（层级系数、窗口、局部性结构）的现实性。

3. **多种子 n=5 偏小。**
   CI 已改用 `t(0.975,4)=2.776`（早期版本误用 1.96 的正态近似，已修复）。
   即便如此，Makespan 改进的区间 [−2.70, +0.79] 仍跨 0，结论不变；
   但命中率提升的区间 [+0.04, +0.72] pp 只是**勉强不含 0**——按 n=5 的
   测量精度，不宜把它单独当作证据。按当前 SD（≈1.4%）估算，要以 95% 把握
   检出 1% 的 Makespan 差异需 10 个以上种子，检出 0.5% 需 40 个以上。
   若要严格检验，建议 n ≥ 20 并改用配对 t 检验或 Wilcoxon 符号秩检验。

4. **合成负载。**
   任务集由 `UUnifastDiscard` + 参数化敏感度生成，未使用真实应用 trace（如 PARSEC/SPEC）。
   结论的外部有效性受限于此。

5. **DAG 结构仍是"伪 DAG"。**
   C1 修复了 `dagID` 硬编码为 0 的问题（现为 `taskId / 5`），使局部性判定有了信号；
   但任务之间**仍无真实前驱/后继依赖**，分配循环也按任务顺序单步推进，
   未模拟 DAG 就绪约束与通信开销。

6. **命名匹配仍脆弱（历史遗留）。**
   `calculateCacheSensitivityBenefit()` 使用精确 `"Cache-Aware".equals(...)`，
   与 `CacheAware_v2` 不匹配，故该项在公平口径下恒为 0。`[5]` 边界测试已把该行为
   **固化断言**，属已知遗留而非本次回归。

7. **单机单次运行。**
   所有数据来自同一台 WSL2 机器、同一 JDK。虽为纯解析模型（不受物理核数影响），
   但浮点累加顺序在极端情况下可能有末位差异。

---

## 8. 复现步骤

```bash
# 1) 全量实验（100 用例 + 可视化，默认 seed=42, cores=8, tasks=60，公平口径）
make -C simulator run

# 2) 仅跑实验、跳过 Python 图表（CI/快速验证）
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment --no-viz

# 3) 多种子统计（均值 / SD / 95% CI）
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment \
     --no-viz --seeds=42,43,44,45,46

# 4) 旧口径复现（仅审计用，**不应作为算法结论**）
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment \
     --no-viz --legacy-metrics

# 5) 自定义规模
java -cp "simulator/lib/*:simulator/classes" AlgorithmComparisonExperiment \
     --cores=8 --cases=200 --tasks=80 --seed=2024

# 6) 单元测试与门禁
make -C simulator test
make check && make symbol-check
```

产物：

- `simulator/result/algorithm_comparison_results.csv`（200 行原始数据）
- `simulator/result/*.png`（3 张图表，300 dpi）

> **可复现性**：固定 `seed=42`、固定参数下输出确定；CSV 与图表可逐次比对。

---

## 9. 结论

### 9.1 公平基线（当前有效结论）

1. 在**两算法共用同一执行/缓存模型**后，CacheAware_v2 相对 WFD 的 Makespan 改进为
   **0.39%（seed=42）/ −0.95% ± 1.23%（5 种子 95% CI）**，**置信区间跨 0 且点估计为负**。
   **不能声称 CacheAware_v2 在 Makespan 上有优势。**
2. 缓存命中率提升 **+0.82 pp（单种子）/ +0.38 pp ± 0.24（多种子）**——统计上为正，但量级不足半个百分点。
3. CacheAware 胜率 **56% / 48.6% ± 5.06%**，与抛硬币不可区分。
4. 响应时间**劣化 1.47%**；CPU 利用率与负载均衡基本持平。
5. **核心结论：先前的 11.66% / +30.21 pp / 96% 胜率主要来自模型口径不一致，而非算法本身。**
   本次改造把该部分"水分"挤出，同时保留了 `--legacy-metrics` 以逐位复现旧值用于审计。

### 9.2 机制解释（为什么收益消失）

- CacheAware **确实**改变了放置行为：同核相邻同 DAG 次数 **6 vs 1**，L1 命中 **2 vs 0**；
- 但当前模型下 60 个任务中有 **40 个落在 L3（×0.95）**，收益空间本身极小；
- 局部性带来的微秒级节省，被"任务被放到更忙的核"造成的排队延迟抵消；
- 旧口径通过"自估 ET 最高减免 50%"与"自评命中率"掩盖了这一权衡。

### 9.3 后续方向（据实修正）

1. **优先修正模型现实性**，而非继续调权重：用真实 trace（PARSEC/SPEC）标定层级系数与窗口大小，
   使 L1 命中率与内存访问代价落在合理区间；
2. 在评分中加入**关键路径判断**——只有落在关键路径上的局部性收益才能缩短 Makespan；
3. 若模型修正后收益依然不显著，则应在论文中**如实报告负结果**，
   并把贡献定位在"工程化的缓存感知调度框架 + 可复现的公平评测方法"，而非"性能提升 X%"。

---

*最后更新：公平基线 C1（2026-10）。单测 72/72 通过；数据源 `simulator/result/algorithm_comparison_results.csv`。*
