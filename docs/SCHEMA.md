# 数据与产物格式约定（`SCHEMA.md`）

本文件定义仓库里**跨进程/跨语言传递的数据格式**与**产物文件命名**。这些不是实现细节：
一旦被脚本、图表、论文或历史记录依赖，改动就是破坏性变更。

本文件覆盖三部分：

| 类别 | 生产者 | 消费者 |
|------|--------|--------|
| [模拟器实验 CSV](#1-模拟器实验-csv) | `simulator/`（Java） | `visualize.py`、`EXPERIMENT_REPORT.md`、下游脚本 |
| [内核性能测试 CSV](#2-内核性能测试-csv) | `linux-6.8-main/test_visualization/`（C） | `visualize_results.py`、`performance_summary.md` |
| [图表文件命名](#3-图表文件命名) | 两条链路各自的 Python 脚本 | 文档、论文插图、CI 产物归档 |

---

## 1. 模拟器实验 CSV

**文件路径**：`simulator/result/algorithm_comparison_results.csv`

**schema 版本**：`1`（见 [`ResultVisualizer.CSV_SCHEMA_VERSION`](../simulator/visualizer/ResultVisualizer.java)）

**编码 / 换行**：UTF-8，`\n`。无 BOM。

### 列定义

| # | 列名 | 类型 | 精度 | 含义 |
|---|------|------|------|------|
| 1 | `Algorithm` | string | — | 算法名。当前取值：`WFD`、`CacheAware_v2` |
| 2 | `TestCase` | int | — | 案例序号，从 `1` 开始，跨利用率级别**连续**编号 |
| 3 | `Makespan` | float | `%.2f` | 该案例所有任务完成的总时间 |
| 4 | `CacheHit` | float | `%.3f` | **实测**缓存命中率 `[0,1]`（由 `SharedCacheModel` 统计，不是按算法名估算） |
| 5 | `TotalExecutionTime` | float | `%.2f` | 该案例所有任务执行时间之和 |
| 6 | `AvgExecutionTime` | float | `%.2f` | 单任务平均执行时间 |
| 7 | `ResponseTime` | float | `%.2f` | 平均响应时间 |
| 8 | `CpuUtilization` | float | `%.3f` | CPU 利用率 `[0,1]` |
| 9 | `LoadBalance` | float | `%.3f` | 负载均衡度 `[0,1]`，越高越均衡 |

> **`CacheHit` 的语义是「实测」**，这是 C1 公平基线轮次的核心结论之一。
> `--legacy-metrics` 下才会退化为按算法名估算，用于复现历史结果；
> 那种情况下的 CSV **不应与公平基线结果混用**。

### 行序

按算法分组，组内按 `TestCase` 升序。**不保证**跨算法的行交错，消费方必须按
`(Algorithm, TestCase)` 自行配对，不要假设「相邻两行是同一案例的两个算法」。

### 元数据旁文件

每次导出 CSV 时，会在同目录写一个 `<csv 文件名>.meta.json`：

```json
{
  "csv_schema_version": 1,
  "csv_file": "algorithm_comparison_results.csv",
  "columns": ["Algorithm", "TestCase", "Makespan", "CacheHit",
              "TotalExecutionTime", "AvgExecutionTime", "ResponseTime",
              "CpuUtilization", "LoadBalance"],
  "algorithms": ["CacheAware_v2", "WFD"],
  "data_rows": 1000,
  "note": "实验参数见 config/*.yaml；本文件只描述 CSV 结构"
}
```

设计取舍：

- **没有把 schema 版本写进 CSV 本身**。CSV 无标准注释语法，多加一行会被
  `pandas.read_csv` 当成数据，直接破坏既有消费者。
- **旁文件不记录实验参数**。参数属于 [`simulator/config/*.yaml`](../simulator/config/README.md)；
  两处都记会变成两个事实来源，迟早互相矛盾。
- 旁文件写入失败**不影响** CSV，仅在 stderr 警告。

### 变更规则

改列名、改列序、改列语义、删列 —— 均为**破坏性变更**，必须同时：

1. 递增 `ResultVisualizer.CSV_SCHEMA_VERSION`；
2. 更新 `ResultVisualizer.CSV_COLUMNS` 与 `CSV_FORMAT`（静态块会校验二者长度一致）；
3. 更新本文件的字段表；
4. 更新 `visualize.py` 里的列名引用；
5. 在 `CHANGELOG.md` 中标注，并说明旧 CSV 是否仍可读。

只改小数位数（`%.2f` → `%.3f`）**不**递增版本：按名读取的消费者不受影响。

---

## 2. 内核性能测试 CSV

**文件路径**：`linux-6.8-main/test_visualization/performance_results.csv`

这是**内核侧**（CFS vs Yat-CAShed 真实运行）的数据，与上面的模拟器 CSV 无关，
不要混用或对比两套数字。

| # | 列名 | 类型 | 精度 | 含义 |
|---|------|------|------|------|
| 1 | `Scheduler` | string | — | `CFS` 或 `Yat-CAShed` |
| 2 | `Thread_ID` | int | — | 测试线程编号 |
| 3 | `CPU_Switches` | int | — | 该线程被迁移/切换的 CPU 次数 |
| 4 | `Execution_Time` | float | `%.3f` | 执行耗时（秒） |
| 5 | `CPU_Affinity_Score` | float | `%.3f` | CPU 亲和性得分 `[0,1]` |

行序：按 `Thread_ID` 分组，组内 `CFS` 在前、`Yat-CAShed` 在后（配对比较的前提）。

---

## 3. 图表文件命名

命名约定：**全小写 + 下划线 + `.png`**，语义前缀在前，指标名在后。
不复用同名文件跨链路，避免「哪张图是哪次跑出来的」无法判断。

### 模拟器链路

输出目录：`simulator/result/`

| 文件名 | 内容 | 生成位置 |
|--------|------|----------|
| `makespan_analysis.png` | Makespan 对比（按利用率级别分组） | [`visualize.py`](../simulator/visualizer/visualize.py) `create_makespan_analysis()` |
| `cache_hit_analysis.png` | 缓存命中率对比 | `create_cache_hit_analysis()` |
| `execution_time_and_win_rate_analysis.png` | 算法执行时间 + 胜率 | `create_execution_time_and_win_rate_analysis()` |

统一参数：`dpi=300`、`bbox_inches='tight'`（论文插图直接可用）。

### 内核链路

输出目录：`linux-6.8-main/test_visualization/img/`

| 文件名 | 内容 |
|--------|------|
| `cpu_switches_comparison.png` | CFS vs Yat-CAShed 的 CPU 切换次数对比 |
| `execution_time_comparison.png` | 执行时间对比 |
| `cpu_affinity_comparison.png` | CPU 亲和性得分对比 |

### README / 文档中引用的静态摘要

| 文件 | 说明 |
|------|------|
| `linux-6.8-main/test_visualization/img/performance_summary.md` | 内核性能测试的表格化摘要（Markdown，非生成物，需手工随测试更新） |

---

## 4. 一致性检查

CSV 列数与格式串的一致性由代码保证，而非靠人记住：

- **模拟器**：`ResultVisualizer` 的静态初始化块会比对
  `CSV_COLUMNS.length` 与 `CSV_FORMAT` 的占位符个数，不一致直接
  `IllegalStateException`（启动即失败，不会产出错位的 CSV）。
- **单元测试**：`tests/YatSimulatorTest.java` 的 `[7]` 段覆盖 YAML 子集与配置校验，
  `[6]` 段覆盖公平基线的指标口径。

## 相关文档

- [`EXPERIMENT_REPORT.md`](EXPERIMENT_REPORT.md) — 模拟器实验设计、统计与结论
- [`simulator/config/README.md`](../simulator/config/README.md) — 实验参数配置
- [`DEPENDENCIES.md`](DEPENDENCIES.md) — 依赖治理
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — 模块与数据流
