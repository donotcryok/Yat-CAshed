# 实验配置（`simulator/config/`）

本目录存放**调度模拟器的实验参数**。目的只有一个：让「跑的是哪组参数」成为一个
可提交、可 diff、可复现的文件，而不是散落在 Java 代码里的常量。

## 加载顺序与优先级

```
代码内默认值  <  配置文件  <  命令行参数
```

配置文件按下列顺序查找，**用到第一个存在的**：

| 顺序 | 来源 | 说明 |
|------|------|------|
| 1 | `--config=PATH` | 显式指定。**文件不存在会直接报错**，不会静默回落 |
| 2 | `$YAT_SIM_CONFIG` | 便于 CI 在脚本里切换 profile |
| 3 | `config/default.yaml` | 相对当前工作目录（通常是 `simulator/`） |
| 4 | `simulator/config/default.yaml` | 从仓库根运行时回退到这个 |

一个都找不到时使用**内置默认值**，其取值与引入配置文件之前的硬编码完全一致，
因此 `make run-quick` 等既有入口的行为不变。

## 新增一组参数（推荐做法）

不要直接改 `default.yaml`，而是新建一个 profile 并在运行时指定：

```bash
cp simulator/config/default.yaml simulator/config/paper.yaml
# 编辑 paper.yaml，然后把实验记录/提交信息里写清 profile 名
java -cp ... AlgorithmComparisonExperiment --config=config/paper.yaml
```

理由：`default.yaml` 是「基线口径」，一旦被就地修改，历史结果就无法解释自己
是在哪组参数下得到的。profile 文件则天然可以把两套参数并存、并做 diff。

## 字段表

### `experiment`

| 键 | 类型 | 默认 | 约束 | 含义 |
|----|------|------|------|------|
| `cores` | int | `8` | ≥ 1 | 处理器核心数，同步到 `SystemParameters.coreNum` |
| `total_test_cases` | int | `100` | ≥ 1 | 总案例数，均分到各利用率级别 |
| `tasks_per_case` | int | `60` | ≥ 1 | 每案例的 DAG 任务数 |
| `seed` | int | `42` | — | 单种子路径的随机种子 |
| `seeds` | list / null | `null` | 非空列表 | 非 null 时启用**多种子聚合**（输出含 95% 置信区间），此时 `seed` 被忽略 |
| `enable_viz` | bool | `true` | — | 是否调用 Python 画图 |
| `legacy_metrics` | bool | `false` | — | 旧的（不公平）度量口径，**仅用于复现历史结果** |
| `utilization_levels` | list[float] | `[0.6,0.8,1.0,1.2,1.5]` | 元素须为有限正数 | 利用率扫描序列，顺序即执行顺序 |
| `high_cache_sensitivity_ratio` | float | `0.7` | `[0,1]` | 高缓存敏感任务占比 |

### `system`

| 键 | 类型 | 默认 | 约束 | 含义 |
|----|------|------|------|------|
| `cache_level` | int | `3` | ≥ 1 | 缓存层级数（不含主存） |
| `level2_core_num` | int | `2` | ≥ 0 | 每 N 核共享一个 L2；`0` 表示不建模 L2 共享 |
| `cc_weights` | list[float] | `[0.5,0.3,0.15,0.05]` | 长度必须为 4，全非负，和 > 0 | 顺序为 `[L1, L2, L3, Memory]` |

## 两个刻意的「不宽容」

1. **未知键直接报错。** 把 `utilization_levels` 拼成 `utilisation_levels` 如果被
   静默忽略，实验会在「以为改了参数」的情况下照旧跑完并产出看着正常的 CSV——
   这比崩溃难查得多。因此解析器对未知键、未知段一律抛异常并给出位置。
2. **不做取值自动纠正。** 例如 `high_cache_sensitivity_ratio: 1.5` 不会被截断成
   `1.0`，而是报错。产出「口径错误但形状正常」的结果是更坏的结果。

## 解析器的能力边界

配置里用到的是一个**零依赖的 YAML 子集**（[`../parameters/YamlSubset.java`](../parameters/YamlSubset.java)），
因为 B2 轮次已把 `simulator/lib/` 收敛到单一 `commons-math3`，不为配置再引依赖。

支持：缩进块映射、缩进列表、行内列表 `[a, b]`、整数/浮点/布尔/`null`、
单双引号字符串、`#` 注释、`---` 文档标记。

不支持且会**明确报错**（不会猜）：Tab 缩进、行内映射 `{a: 1}`、
多行标量 `|`/`>`、锚点/别名 `&`/`*`、合并键 `<<`、标签 `!!`。

## 相关文档

- [`../Simulator.md`](../Simulator.md) — 模拟器总体说明
- [`../../docs/SCHEMA.md`](../../docs/SCHEMA.md) — CSV 字段与图表文件命名的统一约定
- [`../../docs/EXPERIMENT_REPORT.md`](../../docs/EXPERIMENT_REPORT.md) — 实验设计与结论
