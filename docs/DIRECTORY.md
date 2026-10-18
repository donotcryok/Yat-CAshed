# 目录结构与文件职责

```text
Yat-CAShed/
├── README.md                    # 顶层项目说明与快速开始
├── Makefile                     # 顶层编排入口（check/docs-check/simulator/report/...）
├── CONTRIBUTING.md              # 贡献指南
├── CHANGELOG.md                 # 变更日志（Keep a Changelog 风格）
├── LICENSE                      # 分模块许可说明
├── .editorconfig                # 统一编辑器/代码风格
├── .gitattributes               # 行尾归一化（* text=auto eol=lf）
├── .gitignore                   # 忽略规则（含内核产物、模拟器 classes/result）
├── .github/workflows/ci.yml     # CI：仓库检查 + 文档链接 + 符号一致性 + 模拟器回放
│
├── docs/                        # 统一文档中心
│   ├── index.html               #   项目主页（GitHub Pages 入口，单文件无外部依赖）
│   ├── .nojekyll                #   关闭 Jekyll，保证 index.html 按静态页发布
│   ├── README.md                #   文档索引
│   ├── ARCHITECTURE.md          #   项目架构与模块组成
│   ├── CONFLICT_ANALYSIS.md     #   yat_casched.c 冲突分析与解决记录（已解决：路线 A）
│   ├── ROADMAP.md               #   改进与开发方向（优先级 + 执行顺序）
│   ├── DEVELOPMENT.md           #   环境/构建/测试指南
│   ├── OPERATIONS.md            #   操作手册（编译 / 验证 A8 / 读计数器 / 发补丁）
│   ├── EXPERIMENT_REPORT.md     #   模拟器 WFD vs CacheAware_v2 实验报告
│   ├── SCHEMA.md                #   CSV schema / 图表命名 / 变更规则
│   ├── DEPENDENCIES.md          #   simulator/lib 依赖审计
│   ├── PROJECT_AUDIT.md         #   工程化审计快照（问题清单与处理状态）
│   ├── DIRECTIONS.md            #   改进与开发方向建议（面向后续开发）
│   ├── DIRECTORY.md             #   本文件
│   └── archive/                 #   归档区（历史实现文档，非事实来源）
│       ├── README.md            #     归档政策与历史版本关系
│       ├── Yat-CAShed-初赛版实现文档.md
│       └── Yat-CAShed-决赛版实现文档.md
│
├── scripts/
│   ├── check_repo.sh            # 仓库健康检查（冲突标记/空文件/关键文件）
│   ├── check_symbols.sh         # 内核头文件声明 vs 实现定义一致性
│   ├── check_docs.py            # Markdown 相对链接有效性检查
│   ├── check_names.py           # 项目名拼写一致性检查（规范名 Yat-CAShed）
│   └── normalize_eol.py         # 历史文件行尾归一化工具
│
├── linux-6.8-main/              # Linux 6.8 内核源码 + Yat-CAShed 调度类
│   ├── kernel/sched/yat_casched.c         # 调度器核心（AJLR，路线 A）
│   ├── include/linux/sched/yat_casched.h  # 数据结构与接口声明
│   ├── yat_casched优化记录.md              # 迭代优化记录
│   ├── boot_test_scripts/                 # 用户态 C 测试程序（极简 QEMU 环境）
│   │   ├── README.md
│   │   ├── SIMPLE_TEST_README.md
│   │   ├── build_yat_simple_test.sh
│   │   ├── start_yat_simple_test.sh
│   │   └── yat_simple_test.c
│   ├── yat_test_scripts/                  # QEMU 测试环境脚本
│   │   ├── README.md
│   │   ├── create_test_initramfs.sh
│   │   ├── start_test_environment.sh
│   │   ├── start_qemu_simple.sh
│   │   ├── start_qemu_yat_test.sh
│   │   ├── quick_test_guide.sh
│   │   └── manual_test_guide.sh
│   ├── test_visualization/                # 性能测试与结果可视化（独立小工具）
│   │   ├── performance_test.c
│   │   ├── debug_scheduler.c
│   │   ├── visualize_results.py
│   │   ├── performance_results.csv
│   │   ├── README.md
│   │   └── img/                           # 已生成的图表与 summary
│   └── yat_simple_test_env/               # 极简 QEMU initramfs 环境
│
├── simulator/                   # Java 缓存感知调度算法模拟器（Java 17+）
│   ├── AlgorithmComparisonExperiment.java # 实验主入口（8 核 / 100 用例 / 60 任务，可由 config 覆盖）
│   ├── LibraryVerification.java           # 依赖库自检
│   ├── Makefile                           # 跨平台 GNU Make（Linux/macOS/WSL/Git-Bash）
│   ├── build.bat                          # Windows 批处理备用入口
│   ├── Simulator.md                       # 模拟器详细设计文档
│   ├── allocation/   AllocationMethods, WFD, CacheAware_v1, CacheAware_v2
│   ├── analyzer/     PerformanceAnalyzer
│   ├── entity/       Node, RecencyProfileReal
│   ├── generator/    CacheHierarchy, EnhancedTaskGenerator, UUnifastDiscard
│   ├── parameters/   SystemParameters, ExperimentConfig, YamlSubset
│   ├── config/       default.yaml（内置默认值）, README.md（加载顺序与字段约束）
│   ├── visualizer/   ResultVisualizer.java, visualize.py, requirements.txt
│   ├── tests/        YatSimulatorTest.java（102 条断言）
│   └── lib/          commons-math3-3.6.1.jar（唯一运行时依赖）
│
└── report/                      # 论文、报告与参考资料
    ├── paper/                   # 毕业设计 LaTeX 源码（章节文件与主文件平铺）
    │   ├── Yat-CAShed.tex       # 主文件（\input 扁平文件名）
    │   ├── cover.tex / abstract.tex / navigation.tex / references.tex
    │   ├── chapter1-intro.tex … chapter7-difficulties.tex
    │   ├── Yat-CAShed.pdf       # make report 产物（不纳入版本控制）
    │   ├── yat_CAsched.pdf      # 历史成品（留档）
    │   └── img/SYSULogo.png
    ├── prompt_md/               # 分析笔记（ajlr_paper_analysis / plan / test1）
    └── *.pdf / *.pptx / *.docx / *.txt   # 参考论文与早期材料
```

## 说明

- `report/paper/` 的章节文件**与主文件同目录平铺**，主文件使用 `\input{cover.tex}` 这类扁平路径；
  历史上的 `chapters/` 子目录并不存在，相关引用已修复。
- 两份历史「完整实现文档」（原先分别位于 `linux-6.8-main/` 根目录与 `report/paper/`）
  已归档到 [`archive/`](archive/README.md)（原始路径在归档 README 中登记），
  正文保留原样、不再跟随后续修改；当前事实来源是
  [`yat_casched.c`](../linux-6.8-main/kernel/sched/yat_casched.c) 与
  [`ARCHITECTURE.md`](ARCHITECTURE.md)。
- `simulator/result/` 为运行产物目录（CSV + PNG），已被 `.gitignore` 忽略。
- `.gitignore` 中 `*.jar` 规则通过 `!simulator/lib/*.jar` 保留唯一依赖包。
- 内核树占仓库体积绝大部分；如需瘦身，参见
  [`ROADMAP.md`](ROADMAP.md) 中「补丁化 / submodule」方向。
