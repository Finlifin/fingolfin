# 编译器 → C 调用帧 schema

日期：2026-11-01。

从现有 Layout 生成独立二进制 schema，包含参数/绑定/返回偏移和完整结构 ABI：整数、bool、pointer、enum 判别值、record、array。采用有界后序森林，无共享子树或递归引用；所有长度、名字、offset/size/alignment 均按明确 wire 契约验证。Rust 和 C 分别实现校验。

开发 .fgo 升级 version 3，Entry 增加 frame_schema/frame_schema_hash，包 encode/decode 校验 schema 与完整性；旧开发版本拒绝。explain 输出 schema 摘要，C 集成准备脚本保存独立 schema。尚未宣称正式包/schema 全部完成。

验证：

- 含空签名、混合标量、nested record/enum/array、hook binding 的合法输入及截断/变异，共 22,143 例 Rust/C 判定一致；C 端按非对齐地址读取。
- C 默认 schema 测试另有 10,000 轮确定性变异、类型/slot 查询及精确帧初始化。
- C 从新版 record schema 恢复参数位置并构帧，执行生成 ELF 后 C Host counter=37。
- 启用 ubpf 的全量 117 个 Cargo 测试通过，Clippy all-targets warnings-as-errors 和格式检查通过。
- C runtime/uBPF sanitizer 构建 7 个 CTest 项全部通过（含 agreement 语料重放），覆盖 ASan/UBSan/LeakSanitizer；GCC Release 默认 5 个 CTest 项通过。

证据：frame-schema-tests.log、frame-schema-clippy.log、frame-schema-1/asan-ctest.log，以及新生成的 record/aggregate .fgo 和 schema。首轮 Clippy 的 is_multiple_of 风格告警已修正并重新通过。

限制：schema 只是结构 ABI；不能从哈希推导 ELF/schema 行为一致或来源可信。当前 C 测试使用离线提取的 schema/ELF，完整 C .fgo 解析、模型/状态/输出契约、自动 adapter 与真实 cmodel 仍待接入。此工作属于 Rust 编译产物与 C 消费接口，没有恢复 Rust 产品运行时方向。
