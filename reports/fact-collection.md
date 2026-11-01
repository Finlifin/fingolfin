# 生成属性采集与失效验证

日期：2026-11-01。新增 `FgPropertyPath/Read/Provider`、`fg_facts_collect`、`fg_facts_invalidate`。生成 adapter 输出静态属性路径表及 collect wrapper，按 Plan.reads 顺序填平坦 facts。

证据：

- 既有 192 组真实模式 Rust/C 差分改用生成 collector 和一个明确的测试模型 provider，自动解析 object/field 路径后调用生成 predicate/C gate；全部一致，clang UBSan 通过。
- C facts 测试覆盖不同阶段只读取已可用属性；group 路径失效清除自身与 child，root 失效不影响 other 对象；NOT_FOUND 单槽 unknown；provider 错误、预算增加、预算不足时全量 unknown。
- `cargo test --offline --test gate_codegen --test adapter_codegen` 五个测试通过，Clippy all-targets warnings-as-errors 通过。
- facts、gated_binding、generated_adapter 三项 ASan/UBSan/LeakSanitizer CTest 通过：[日志](fact-collection-1/asan-ubsan.log)。

复现沿用上述 Rust 测试及 `ctest --test-dir BUILD -R 'facts|generated_adapter|gated_binding' --output-on-failure`。

限制：provider 仍需由实际模型实现，测试 provider 不是 tt-emule 集成证据。父路径类型、对象固定和一致模型快照由 provider 契约保证。外部模型修改必须同步旧租约，不能以“已发布 invalidation”替代旧事实读者排空。真实模型安全点/失效通知/代际协调尚未完成。
