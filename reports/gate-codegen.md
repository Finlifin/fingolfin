# 编译器 Plan 到 C topology 验证

日期：2026-11-01。新增 `gate::lower_c`、`CPlan/CNode/CTest`。`emit-adapter` 的 C 头包含 `fg_generated_gate_plan()`，JSON 命令结果包含精确 predicate 描述与来源属性/阶段。

`tests/gate_codegen.rs`：

- TRUE、空 AND、空 OR、NOT、三子节点 AND/OR、嵌套组合，分别枚举三个属性的 unknown/false/true 与四个阶段，共 756 组。
- 预期值来自 Rust `Plan::evaluate`，实际值来自生成节点编译成的 C 程序，调用真实 `fg_gate_evaluate`；clang UBSan 下全部一致。
- 检查保留的属性/阶段/test 描述；拒绝未知版本、无效读取索引、过量节点及过深源表达式。

运行 `cargo test --offline --test gate_codegen --test adapter_codegen --test gate`，共 9 个相关测试通过；`cargo clippy --offline --all-targets -- -D warnings` 通过。

生成 record adapter 后，C 端到端测试改用生成的 gate plan 创建 gated binding；真实 SDK→Attachment→C VM 执行通过。generated_adapter 与 gated_binding 在 ASan/UBSan/LeakSanitizer 下通过：[日志](gate-codegen-1/asan-ubsan.log)。record 是无模式普通函数，因此这里验证的是生成 TRUE plan 的装配；复杂拓扑语义由前述差分覆盖。

范围：差分使用有效 facts 和充足预算；C 非短路求值在错误/预算耗尽时仍可比 Rust 更多放行。尚未生成模型属性读取或 literal/range C 谓词，也未完成真实 cmodel 可用性/失效通知。不能把 topology 转换视为完整自动门控完成。
