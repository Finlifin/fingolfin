# SDK ABI 验证

2026-11-01，Linux x86-64 little-endian，Clang/Clang++ 23.1.4，当前 Rust 工具链。

`tests/sdk_abi.rs` 编译 C11 和 C++20 共享库并从 Rust 动态加载。两种语言均通过：ObjectKey/CallHeader/RuntimeV1 的 size/align/offset 比较；32 字节 ObjectKey 按值参数和返回；native→Rust callback 按值调用；函数表版本/空 callback 拒绝；frame 小于头和 hook-specific 超长拒绝；对象 generation 不符拒绝；gate false 时 frame 构造 0 次；gate true 时构造 1 次；宏输入无重复求值。

全量 Cargo 测试 42 个通过；日志 sdk-tests.log。Clippy 严格检查通过；日志 sdk-clippy.log。

尚未证明：模型类型自动生成、真实 CB→SDK→VM、热更新/回收、DSO 共享注册表、完整 frame 和 helper 指针验证。
