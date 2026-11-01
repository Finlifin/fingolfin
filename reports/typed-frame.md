# 类型化开发调用帧

日期：2026-11-01。

新增 frame::Layout / Frame。布局从 CheckedProgram 的类型、结构体字段偏移和 enum 表示构建；BPF 代码生成也改为使用同一 Layout 生成静态断言，消除两套独立偏移计算。参数和绑定区后追加结果，保留现有 16 字节预算/深度/错误头及帧格式。

支持整数范围、bool、enum 名称/判别值、嵌套结构体、结构体中的数组以及 opaque pointer。显式 frame 容量在分配前检查。prepare 清零 padding 和旧结果，检查参数数量、字段、数组长度及命名绑定；返回值按原类型解码。vm::Function 在加载时重新生成并比较帧头，绑定实际 VM 与该布局，参数失败不执行 VM。

验证：启用 ubpf 的全量 115 个测试通过（含 4 个真实 Rust Host/VM 测试），Clippy all-targets warnings-as-errors、格式检查通过。日志为 typed-frame-tests.log 与 typed-frame-clippy.log。新增用例检查混合宽度布局/padding、范围拒绝、容量、hook 命名绑定，以及含 bool、signed enum、i16 极值数组、u64::MAX 指针的结构体在真实 VM 中往返。错误预算、错误参数后的重用及不一致 ABI 拒绝也通过。既有原生代码生成和 BPF 编译测试在全量测试中重新运行。

限制：布局权威仍来自已检查源码/模型，包内完整可恢复类型 schema 尚未实现。hook 匹配和生命周期仍由调用方负责；没有宣称自动 cmodel adapter 已完成。复合 Value 解码会分配内存。该改动未升级 .fgo version，也未改变开发帧 ABI。
