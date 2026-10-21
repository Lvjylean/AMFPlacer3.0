# 增量编译配置键更正（77）

规范键名为 `IncrementalCompilation`，取值 true / false。Python 作业支持 JSON 布尔值或小写字符串；AMF 原有扁平配置继续使用字符串。

- 缺省或 false 不启用增量上下文，默认行为不变。
- 旧拼写 `IncremenralCompilation` 兼容读取；进入解析器后规范化为新键，生成配置和 AMF 配置日志只输出新键。
- 两键同时存在且值等价可接受；值冲突或非法布尔值报错，禁止静默选一边。
- C++ 静态缓存 compose、全量 DFX 硬区域、增量静态上下文以及 Python 单模块、多模块、并行、全量 DFX 和普通 full-run 入口保持一致。
- 启动器要求 DFX 二进制声明 `incremental_compilation_key_schema=1`，防止旧程序忽略新键后丢失静态约束；普通模式无需此能力。
- `DFX placement mode=full|incremental`、BoundaryAwareClustering 及其他算法配置不变。规范名称不改变各自的启用条件。

本副本基于 76 的冻结源码及 overlay。历史实验、部署、76 源码与 validated-38 配置引用保留原样。仅构建和配置/约束/缓存回归，不重新综合或运行完整布局布线，不提交或上传 Git。
