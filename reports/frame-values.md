# C 帧输入值校验

日期：2026-11-01。实现 `fg_frame_schema_validate_inputs`，接入 Executor 和编译器生成的 binding encoder。结构化 schema 提供 bool、enum 判别值、record 字段及 array 元素布局；递归访问不分配，预算按值及 enum 候选计费。输入必须配合已验证且不可变的 schema。

证据：

- `cargo test --offline --test adapter_codegen --test bpf_codegen` 通过。
- 新 adapter 测试实际生成含 `[bool;2]` 与 `enum E:i8` 的结构体 encoder，用 clang/UBSan 编译执行。遍历全部 256 个 bool 字节和 256 个 enum 字节，检查只接受 bool 0/1 与 enum -128/127；另验证零遍历预算、错误长度和结果槽不作为输入校验。
- `cargo clippy --offline --all-targets -- -D warnings` 与格式检查通过。
- 重新生成 record adapter 后，完整 16 项 C ASan/UBSan/LeakSanitizer CTest 通过：[日志](frame-values-1/asan-ubsan.log)。嵌套值枚举测试由上述 Rust 测试驱动的独立 UBSan C 程序执行。

Executor 在执行槽 admission 前验证；拒绝不修改输入或 result。生成 encoder 在复制后验证，语义错误允许留下已编码 scratch，但 binding 不会执行它。两条入口目前各自使用一百万 visits 的固定上限，未优化重复检查，未进行性能门禁。

限制：不校验指针权限/生命周期，不读取结果槽，不将 padding 当作语义字段。该检查不替代完整模型 adapter、pattern/gate 或正式包身份契约。
