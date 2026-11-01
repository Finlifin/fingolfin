# C 会话注册表验证

2026-11-01，新增 C `fingolfin_agent` 会话核心，连接已有 C Program loader。真实包测试验证加载/卸载、先校验再发布、固定容量、源码 pin、序号重复拒绝、generation 条件更新、关闭以及独立 Program 引用跨卸载/销毁保活。

GCC Release 通过，见 [release.log](c-session-1/release.log)。Clang ASan/UBSan/LeakSanitizer 下 session、program、cli_inspect 三项通过，见 [sanitizer.log](c-session-1/sanitizer.log)。测试为单控制器串行模型，不声称会话库支持并发调用；Program 引用计数复用既有实现。

接口契约与限制见 [Agent 文档](../agent/README.md)。本轮完成类型化会话核心，尚未实现 JSON-RPC transport、Agent 服务进程、挂载/排空、远端连接与 SSH/GDB 集成，不能作为完整 FNG-021 验收。
