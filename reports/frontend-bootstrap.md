# 前端启动验证记录

日期：2026-11-01。源码位于当前工作树（尚无 commit），依赖版本见 Cargo.lock。

环境：Linux x86-64；Cargo 1.99.0-nightly (7c83d4cc0 2026-07-29)；Clang 23.1.4，目标列表包含 bpf/bpfel/bpfeb。Clang 信息只证明工具存在，尚未执行 C→BPF→VM 实验。

本轮执行并通过：

```sh
cargo test --offline
cargo clippy --offline --all-targets -- -D warnings
```

测试共 15 个：

- syntax（9）：计划原文多入口示例、表达式优先级、范围/析取/否定/绑定、类型声明、UTF-8 span、非法输入、控制流、关键字边界、含注释路径及整数边界。
- source（4）：内容身份确定性、模块命名、冲突和多文件错误、非法 source/未知清单字段/缺少 lib、符号链接拒绝（部分主题合并在同一测试）。
- cli（2）：parse/inspect 的 JSON 输出；不存在的输入及未实现命令失败。

中间失败已修复：PEG 自动空白影响 pub/or 关键字边界；文件/目录冲突产生重复诊断；负整数最小值被前缀运算拆分；Clippy 的 filter-next 建议。最终重跑全部通过。

限制：本报告不是 M1 退出证明；没有执行语义检查、ABI、VM、运行时、tt-emule 或 GDB 测试。后续阶段须按项目计划第 11.3 节生成包含源码版本和原始日志的完整验收包。
