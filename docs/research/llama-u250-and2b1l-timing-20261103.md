# AND2B1L 组合传播修复

基线为独立接口适配副本的 `76070fc21cf141f3969bde05841393417d5b8e4d`。用户要求单独修正组合逻辑被当作时序端点的问题；本次唯一生产源码改动位于 `DesignInfo.h` 的端点分类。原 SA 副本及前两批实验记录不修改。

## 语义和修改

AND2B1L 是占用 CLB 锁存器资源的组合门，DI 和 SRI 均向 O 传播。物理分类 `isFF()` 仍包含它，继续使用既有 BEL 映射、SRI 网络/极性和半 SLICE 控制兼容检查；`isTimingEndPoint()` 排除 AND2B1L。这使时序图和聚类图均穿过该单元，相关拓扑遍历也不再在此结束。

现有建图没有过滤 SRI，因而不需要改动引脚方向或删除 `PinType_SR`。保留该物理引脚类型，避免破坏共享 SRI 的打包约束。普通 FF、LDCE、I/O 等原有端点分类保持原样。

依据：[AMD AND2B1L 原语说明](https://docs.amd.com/r/2024.2-English/ug974-vivado-ultrascale-libraries/AND2B1L)。本次不重新拟合延迟，仍使用现有普通 Pro 的组合传播估计。主周期 3.332 ns、benchmark 派生时钟、`ProPrimitiveTiming=false`、`SllRegionPolicy=preserve` 和全部物理约束不变。

## 验证

- 构建成功：`builds/build-20261103-025805-602903-76070fc2/build/AMFPlacer`，4 个编译线程，75.21 秒。
- 同一专项测试链接修复前的冻结生产库，正确复现“AND2B1L incorrectly cuts timing path”；链接修复后的库通过。
- 专项测试从真实输入解析器及初始打包器构建网表，再调用生产建图代码。DI/SRI 轮流成为较慢输入，验证两级 AND 链、扇出、前向延迟、反向时间预算、关键路径回溯。覆盖复合 I/O 边界开/关两种模式、时序/聚类两种图以及重复并行传播。
- 两个 AND 采用相反 SRI 极性：共享半 SLICE 被拒绝，独立半 SLICE 允许；FF 物理资源和控制模式 3 保留。既有复合 I/O 时序与完整 FF 极性打包回归通过。
- llama 真实输入沿用第二批冻结配置及网表，仅把输出目录指向新验收目录。初始打包和两种图检查共 41.09 秒。四个 DDR4 校准 MicroBlaze 内的 AND2B1L 均不再为端点；前向层级为 6、反向层级为 2，各保留 DI/SRI 两条输入边及 10 条 O 扇出边。

专项测试源文件是 `src/tests/check_and2b1l_timing.cc`，入口为 `tests/check_and2b1l_timing.py`。支持新构建中的 CMake 目标 `checkAND2B1LTiming`；本轮使用 `scripts/diagnostics/compile_and2b1l_timing_check.py` 在独立验收目录编译当前测试，链接冻结构建的生产头文件和库，不修改已有源码快照。测试源及可执行文件哈希记录在 checker manifest。

验收目录（相对副本父目录）：

- `validation/and2b1l-baseline-regression-20261103-030032`
- `validation/and2b1l-acceptance-20261103-030038`
- `reports/and2b1l/`：最终来源、输入哈希、状态、逐单元核验和补丁。

初次专项测试编译失败记录保留在 `validation/and2b1l-baseline-regression-20261103-025914`，修正测试调用接口后重新验收；生产修复未因此改变。未运行新的全局布局或 Vivado 完整布局布线，不能据此声称时序收敛或布通。DCP 只保存在服务器。
