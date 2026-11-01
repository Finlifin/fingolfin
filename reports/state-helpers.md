# 计数和输出接口贯通

日期：2026-11-01。

增加保留接口 builtin.counter_add(u32,u64) 与 builtin.emit(u32,u64)，void 返回。语义检查验证参数与 void 用法，IR 使用独立 Helper 节点，参考解释器仅在显式附加 Host 时允许执行。Host 使用预分配 BoundedMap/Counter/BoundedQueue，计数槽必须提前初始化。

BPF 固定 helper ID 1/2，helper 非零状态变成执行错误 6，并跳过后续语句。native C 使用宿主 fg_native_helper 入口和显式 context。实际 ELF 指令读取 helper IDs，.fgo version=2 保存并交叉核对清单；旧开发包 version=1 明确不再读取，须重建。校验和仍不代表信任签名。

真实 uBPF 初次执行因缺少 safe helper metadata 被拒绝，补充 scalar-return descriptor 后通过。runner 由执行 cookie 获取每个调用的宿主 State，不使用全局当前状态或 TLS。覆盖无注册加载拒绝、两实例交替调用、正确 counter/emit、队列满、计数溢出和未知槽。状态及输出按 helper 独立提交，后续失败不回滚已完成效果。

验证：106 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。包括类型拒绝、缺少 host、host 状态/输出、失败停止后续效果、ELF/helper 清单一致性及 eval CLI 参数。真实 VM 原始记录在 state-helpers-1/，Cargo/Clippy 日志为 state-helper-tests.log/state-helper-clippy.log。

限制：C runner 的固定状态仍是独立 adapter；Rust 容器与完整 VM loader 未直接链接。尚需语言状态声明、实例绑定/回收自动化、histogram/map helper、完整输出 schema 以及真实 cmodel 集成；不声称 FNG-018 已完成。
