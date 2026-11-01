# 显式函数 adapter 自动生成验证

日期：2026-11-01。新增 `codegen::generate_adapter` 和 CLI `emit-adapter`。从 checked types/IR/frame Layout 生成 C11 原生 SDK 帧及 binding encoder；使用与 `.fgo` 相同的机器 schema，逐字节核对后编码。

验证：

- `cargo test --offline --test adapter_codegen --test bpf_codegen` 通过。新测试检查确定性生成、未知函数拒绝，空参数、标量/指针、嵌套 record/enum/数组的严格 C11 编译；直接数组参数明确报需 struct wrapper。原 BPF 编译回归通过。
- `cargo clippy --offline --all-targets -- -D warnings` 通过；`cargo fmt` 已执行。
- 实际 `emit-adapter` 生成 record header，与现有 record v3 `.fgo` 配合，在 C 测试中直接调用生成 encoder。SDK route → binding → Attachment → C VM 的 counter/output 为 37；错误 schema 拒绝且保留目标 frame。
- 新端到端用例在 C runtime/patched uBPF 的 ASan/UBSan/LeakSanitizer 构建通过：[日志](generated-adapter-1/asan-ubsan.log)。

复现：使用 `integration/c-runtime/prepare.py` 离线生成新版 fixture，将 runtime CMake 的 `FG_TEST_ADAPTER_DIR` 指向该目录；同时启用 package/uBPF，运行 `ctest --test-dir BUILD -R generated_adapter --output-on-failure`。

范围：自动生成的是显式函数参数编码，不是完整模型 adapter。调用方仍提供 hook 映射、bindings、合法值与指针生命周期；尚无自动 pattern/gate、cmodel 生命周期或结果回填。当前一个翻译单元包含一个生成 adapter，schema 比较发生在每次编码，未做性能门禁。
