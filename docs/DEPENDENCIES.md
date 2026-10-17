# 依赖审计报告（`simulator/lib/`）

> 本报告由 `jdeps --multi-release 17 -verbose:class` + 源码 `import` 静态分析生成，
> 对应 [ROADMAP B2「依赖治理」](ROADMAP.md)。

## 1. 结论速览（已执行精简）

| 项 | 精简前 | 精简后 |
|----|--------|--------|
| `simulator/lib/` JAR 总数 | 30 | **1** |
| 实际被依赖的 JAR | 1（`commons-math3-3.6.1.jar`） | 1 |
| 未被依赖的 JAR | 29 | **0** |
| `lib/` 目录总大小 | 约 77 MB | **2.2 MB** |
| 可精简比例 | ≈ 97% | ✅ 已精简 |

> **执行状态：✅ 已于本次变更中移除 29 个死依赖**（保留 `commons-math3-3.6.1.jar`）。
> 移除后 `make simulator-verify` 全绿（58/58 断言）、全量实验数值与移除前**逐位一致**
> （Makespan 155058.5 vs 176043.8、命中率 73.81% vs 43.61%），确认零影响。
> 29 个 JAR 均已被 git 跟踪，如需回滚可从历史检出。

**唯一真实依赖**：`commons-math3-3.6.1.jar`，仅用于提供
`org.apache.commons.math3.util.Pair`（在
[`simulator/entity/RecencyProfileReal.java`](../simulator/entity/RecencyProfileReal.java)
等 3 处被引用）。

## 2. 使用中（保留）

| JAR | 大小 | 用途 | 证据 |
|-----|------|------|------|
| `commons-math3-3.6.1.jar` | 2.2 MB | `org.apache.commons.math3.util.Pair` | jdeps 输出 `lib/commons-math3-3.6.1.jar`；源码 `import org.apache.commons.math3.util.Pair` |

> 备注：本项目仅使用 `Pair` 这一个极简容器类。若希望「零第三方依赖」，可用
> 一个自实现的 `Pair<K,V>` 替换（见 §5 建议 B），从而把 `lib/` 完全清空。

## 3. 已移除的死依赖（共 29 个，留档备查）

以下 JAR 经 jdeps 与源码 `import` 双重确认**零引用**，且多为图算法 / Web 图压缩 /
脚本引擎等与本模拟器无关的重型库：

| JAR | 大小 | 领域 |
|-----|------|------|
| `jython-standalone-2.7.2.jar` | **42 MB** | Python 引擎（本项目直接调 `python3` 子进程，不内嵌 Jython） |
| `fastutil-8.5.2.jar` | **23 MB** | 高性能集合库 |
| `guava-29.0-jre.jar` | 2.7 MB | 通用工具库 |
| `jackson-databind-2.18.2.jar` | 1.6 MB | JSON 绑定 |
| `jgrapht-core-1.5.1.jar` | 1.2 MB | 图论核心 |
| `jgraphx-4.1.0.jar` | 816 KB | 图形 UI |
| `jackson-core-2.18.2.jar` | 584 KB | JSON 核心 |
| `dsiutils-2.6.17.jar` | 504 KB | DSI 工具 |
| `commons-lang3-3.9.jar` | 496 KB | 语言工具 |
| `logback-core-1.2.3.jar` | 464 KB | 日志 |
| `sux4j-5.2.3.jar` | 448 KB | succinct 数据结构 |
| `webgraph-3.6.10.jar` | 376 KB | Web 图压缩 |
| `jgrapht-io-1.5.1.jar` | 352 KB | 图 IO |
| `antlr4-runtime-4.8-1.jar` | 332 KB | 解析器运行时 |
| `webgraph-big-3.6.6.jar` | 292 KB | Web 图压缩 |
| `logback-classic-1.2.3.jar` | 284 KB | 日志 |
| `commons-text-1.8.jar` | 204 KB | 文本工具 |
| `jheaps-0.13.jar` | 152 KB | 堆结构 |
| `jgrapht-unimi-dsi-1.5.1.jar` | 88 KB | JGraphT 扩展 |
| `jsap-20210129.jar` | 88 KB | 命令行解析 |
| `json-simple-1.1.1-javadoc.jar` | 88 KB | 文档包（非二进制） |
| `jackson-annotations-2.18.2.jar` | 80 KB | JSON 注解 |
| `jgrapht-guava-1.5.1.jar` | 48 KB | JGraphT 扩展 |
| `slf4j-api-1.7.30.jar` | 44 KB | 日志门面 |
| `jgrapht-opt-1.5.1.jar` | 40 KB | JGraphT 扩展 |
| `jgrapht-demo-1.5.1.jar` | 36 KB | 示例 |
| `json-simple-1.1.1.jar` | 24 KB | JSON |
| `json-simple-1.1.1-sources.jar` | 20 KB | 源码包（非二进制） |
| `jgrapht-ext-1.5.1.jar` | 8 KB | JGraphT 扩展 |

## 4. 验证方法（可复现）

```bash
cd simulator
# 1) 编译
javac -cp "lib/*" -d classes $(find . -name '*.java' -not -path './tests/*')
# 2) 类级依赖抽取
jdeps --multi-release 17 -verbose:class -cp "lib/*:classes" classes \
  | awk -F'->' '/->/ {print $NF}' | awk '{print $1}' | sort -u \
  | grep -v '^java' | grep -v '^allocation\|^analyzer\|^entity\|^generator\|^parameters\|^visualizer'
# 期望输出：lib/commons-math3-3.6.1.jar  （仅此一项）
```

补充：`AlgorithmComparisonExperiment` 通过 `ProcessBuilder` 调用外部 `python3`
执行 [`visualizer/visualize.py`](../simulator/visualizer/visualize.py)，
因此**不**需要 Jython；`lib/*.jar` 中与 Python/图/Hadoop 生态相关的包全部冗余。

## 5. 建议

- **A. 渐进式精简（推荐，低风险）**：保留 `commons-math3-3.6.1.jar`，
  其余 29 个 JAR 从版本库移除（或迁至 `docs/archive/` / 由构建脚本按需拉取）。
  预计 `lib/` 从 77 MB 降至 2.2 MB。
- **B. 彻底零依赖（可选）**：以自实现 `Pair` 替换 `commons-math3`，
  可完全删除 `lib/`，`javac`/`java` 无需 `-cp lib/*`。
- **C. 构建期防回归**：在 CI 中增加 `jdeps` 校验，若源码引入未声明依赖则失败
  （可作为后续 ROADMAP B2 的增强项）。

> ✅ **已按建议 A 执行**：删除前先跑通 `make simulator-verify`（58 项断言 + 快速实验），
> 确认 `visualize.py` 走 `python3` 子进程而非 Jython，再移除 29 个 JAR。
> 若后续需要恢复，可从 git 历史检出对应文件。
