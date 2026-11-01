# C CLI 持续会话验证

2026-11-01，`fgctl session ID -- EXEC ARGS...` 通过 POSIX 子进程双向管道接入 C Agent。CLI 从命令生成请求，发送经过本地校验的包与源码 pin，跟踪 sequence/generation，核对响应身份后输出 JSON。目标程序通过 argv 启动，不隐式经过 shell。

真实 C CLI→C Agent 验证 status/load/list/unload/list/close，确认序号 1–6 与代际 1/2/2/3/3/4，路径包含空格仍正确。测试另覆盖远端错误后继续查询、非法命令、缺失/坏包、未终止或过长命令、NUL、错误响应 ID/会话/代际、重复键、截断/非法 UTF-8、子进程启动失败与异常退出。

GCC Release 通过，见 [release.log](c-cli-session-1/release.log)。cli_inspect、agent_stdio、cli_session 三项 Clang ASan/UBSan/LeakSanitizer 通过，见 [sanitizer.log](c-cli-session-1/sanitizer.log)。无响应子进程的独立实测验证 30 秒期限和退出清理，见 [timeout.log](c-cli-session-1/timeout.log)。

用法与限制见 [CLI 文档](../cli/README.md)。本轮实测本地通道；SSH/GDB、hook 挂载、事件数据通道与跨连接恢复未完成。
