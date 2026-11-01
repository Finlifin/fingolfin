# 公共工具库验证

日期：2026-11-01。

- `budget`：扣费前检查、不溢出、不自动补充；接入模式参考求值器。
- `artifact`：同目录独占创建临时文件、写入与同步后 rename 发布、失败时清理；接入 ABI 输出。并发时最后成功 rename 生效；不保留目标文件权限，父目录同步失败可能发生在发布后。
- `text`：UTF-8 byte offset 到一基行号和 Unicode scalar 列号；支持 CRLF 行摘取，拒绝字符内部偏移和越界。列号不是终端显示宽度。
- `hash`：SHA-256 和 little-endian u64 长度前缀字段哈希；替换源码加载与编译检查的重复实现，保持原有编码格式。

`cargo test --offline`：57 个集成测试通过。新增验证覆盖预算极值、Unicode/CRLF/EOF、并发读取完整产物、失败清理、symlink 替换语义、标准 SHA-256 向量及字段边界。

`cargo clippy --offline --all-targets -- -D warnings` 与 `cargo fmt --check` 通过。原始日志为 `util-tests.log` 和 `util-clippy.log`。

这些库不等同于观察语言的标准库；输出、聚合、有界状态以及完整编译和执行链路仍需完成。
