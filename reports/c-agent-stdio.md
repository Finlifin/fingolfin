# C Agent stdio 验证

2026-11-01，C `fg-agent` 可执行程序已连接会话核心，支持版本化 JSON 请求、查询/加载/卸载/关闭和逐行响应。真实进程测试输入实际编译 `.fgo`，验证正常状态、重复请求、旧代际、坏包、关闭后拒绝和 EOF 释放存活 Program。

边界覆盖：重复键及转义同名键、未知字段/方法、错误类型/版本、非法 UTF-8、NUL、非规范及溢出 ID、尾随 JSON、超过 64 MiB 的帧及后续恢复、未终止行在断开时不执行、非法启动参数。

GCC Release 通过，见 [release.log](c-agent-stdio-1/release.log)。Clang ASan/UBSan/LeakSanitizer 下 session 与 agent_stdio 两项通过，见 [sanitizer.log](c-agent-stdio-1/sanitizer.log)。这是相关测试，不是全量回归或性能验收。

[协议文档](../agent/protocol.md)说明受限 JSON-RPC 风格和重试语义。尚未实现 CLI 远端调用、hook 挂载、数据流、SSH/GDB、跨连接恢复；完整 FNG-021/FNG-022 仍未完成。
