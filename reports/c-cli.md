# C CLI 首个命令验证

2026-11-01，`fgctl inspect` 已接入 C loader，使用真实 `.fg` 编译包验证 JSON 身份与入口/helper 信息、两个正确源码 pin、错误/非法 pin、重复/缺失参数、未知命令和选项、缺失文件、空包、截断、尾随垃圾与错误 magic。

GCC Release 测试通过，记录见 [release.log](c-cli-1/release.log)，实际输出见 [inspect.json](c-cli-1/inspect.json)。Clang ASan/UBSan/LeakSanitizer 下 `package` 与 `cli_inspect` 两项 CTest 通过，见 [sanitizer.log](c-cli-1/sanitizer.log)。本轮仅复验相关测试，没有宣称全量回归。

实现与用法见 [CLI 文档](../cli/README.md)。该命令是离线控制工具的首个功能；Agent、JSON-RPC 会话、挂载/卸载、SSH/GDB 和数据通道仍待实现。
