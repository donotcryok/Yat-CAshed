# 目录结构与文件职责

```text
Yat-CAShed/
├── README.md                    # 顶层项目说明
├── Makefile                     # ★ 顶层编排入口（check/simulator/report/...）
├── CONTRIBUTING.md              # ★ 贡献指南
├── LICENSE                      # ★ 许可说明（分模块）
├── .editorconfig                # ★ 统一编辑器/代码风格
├── .gitignore                   # 忽略规则
├── .github/workflows/ci.yml     # ★ CI：冲突守卫 + 模拟器编译校验
│
├── docs/                        # ★ 新增：统一文档中心
│   ├── README.md                #   文档索引
│   ├── ARCHITECTURE.md          #   项目架构与模块组成
│   ├── CONFLICT_ANALYSIS.md     #   yat_casched.c 冲突分析与解决记录（已解决：路线 A）
│   ├── ROADMAP.md               #   改进与开发方向
│   ├── DEVELOPMENT.md           #   环境/构建/测试指南
│   ├── DIRECTORY.md             #   本文件
│   └── archive/                 #   归档文档说明
│
├── scripts/
│   └── check_repo.sh            # ★ 仓库健康检查脚本
│
├── linux-6.8-main/              # Linux 6.8 内核源码 + Yat_Casched 调度类
│   ├── kernel/sched/yat_casched.c        # 调度器核心（AJLR，冲突已解决）
│   ├── include/linux/sched/yat_casched.h # 数据结构与接口声明
│   ├── Yat_Casched开发文档.md             # 初赛版实现文档
│   ├── yat_casched优化记录.md             # 迭代优化记录（变更日志）
│   ├── boot_test_scripts/                # 用户态 C 测试程序
│   │   ├── README.md
│   │   ├── SIMPLE_TEST_README.md
│   │   ├── yat_simple_test.c             # 有效测试源码
│   │   ├── yat_scheduler_test.c          # ⚠️ 空文件（待补齐或删除）
│   │   └── test_simple_history_validation.c  # ⚠️ 空文件
│   ├── yat_test_scripts/                 # QEMU 测试环境脚本
│   │   ├── README.md
│   │   ├── create_test_initramfs.sh
│   │   ├── start_test_environment.sh
│   │   ├── start_qemu_simple.sh
│   │   ├── start_qemu_yat_test.sh
│   │   ├── quick_test_guide.sh
│   │   └── manual_test_guide.sh
│   ├── test_visualization/               # 性能测试与结果可视化
│   │   ├── performance_test.c
│   │   ├── debug_scheduler.c
│   │   ├── visualize_results.py
│   │   ├── README.md
│   │   └── img/                          # 生成的图表
│   └── yat_simple_test_env/              # 极简 QEMU 测试环境
│
├── simulator/                   # Java 缓存感知调度算法模拟器
│   ├── AlgorithmComparisonExperiment.java # 实验主入口
│   ├── LibraryVerification.java           # 依赖库自检
│   ├── Makefile                          # ⚠️ Windows cmd 语法
│   ├── build.bat                         # Windows 构建
│   ├── Simulator.md                      # 模拟器详细文档
│   ├── allocation/  WFD, CacheAware_v1/v2, AllocationMethods
│   ├── analyzer/    PerformanceAnalyzer
│   ├── entity/      Node, RecencyProfileReal
│   ├── generator/   CacheHierarchy, EnhancedTaskGenerator, UUnifastDiscard
│   ├── parameters/  SystemParameters
│   ├── visualizer/  ResultVisualizer.java, visualize.py, requirements.txt
│   └── lib/         第三方 JAR（30+）
│
└── report/                      # 论文、报告与参考资料
    ├── paper/                   # 毕业论文 LaTeX 源码
    │   ├── Yat-CAShed.tex       # 主文件
    │   ├── chapter1..7*.tex     # 各章节
    │   ├── Yat-CAShed_new.tex   # ⚠️ 空文件
    │   ├── Yat_Casched_完整实现文档.md  # 决赛版实现文档
    │   └── yat_CAsched.pdf      # 编译成品
    ├── prompt_md/               # 分析笔记（ajlr_paper_analysis / plan / test1）
    └── *.pdf / *.pptx / *.docx / *.txt   # 参考论文与早期材料
```

> 说明：标 `★` 者为本次工程化整理新增；标 `⚠️` 者为已知待处理项。
