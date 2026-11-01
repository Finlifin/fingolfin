# C Program/Executor 验证

日期：2026-11-01。范围：开发 v3 包的显式函数执行和资源所有权；正式 Attachment、模式/gate 自动分发与模型生命周期尚未交付。

新增 `program.h`：Program 拥有不可变验证包及原子引用计数；Executor 按函数标识持有 Program 引用与独立 VM 槽池。helper 授权来自显式 policy，与包内实际要求比对。VM 容量来自已验证 schema，执行要求精确帧长。

`runtime/tests/program.c` 验证：

- 非法包加载、未知入口、helper policy 拒绝均不发布对象。
- Executor 保活 Program；调用方释放初始 Program 引用后执行仍有效。
- 错误帧长保留 result；helper 错误后仍可执行成功；20 次 VM 指令预算错误后可关闭排空，槽未泄漏。
- 四线程共享一个四槽 Executor，各自独立 Host，8,000 次成功/失败交替执行，计数及输出逐项验证。
- 关闭和销毁第一个 Executor 后，同 Program 的第二个独立 Executor 再完成 2,000 次调用。
- 未关闭 destroy 返回 BUSY；关闭后执行返回 CLOSED 且保留 result。

完整 13 项 CTest 在 Clang 23 ASan/UBSan/LeakSanitizer 下通过：[日志](c-program-1/asan-ubsan.log)。新增 Program 测试在 GCC 16 TSan 下通过：[日志](c-program-1/tsan.log)。C runtime 与 patched uBPF 均使用对应插桩；系统 json-c/OpenSSL 未重新插桩。

复现：按 runtime README 构建 scratch uBPF，启用 `FG_WITH_UBPF`、`FG_UBPF_SCRATCH` 和 `FG_WITH_PACKAGE`，指定 record fixture `FG_TEST_PACKAGE`，构建后运行 `ctest --test-dir BUILD -R program --output-on-failure`。状态/输出配置仍由调用方提供，不代表正式包已经包含完整配置契约；共享 Host 的同步仍是调用方责任。
