# C 开发函数 Attachment 验证

日期：2026-11-01。范围：显式函数调用的身份、预算、Host 隔离和生命周期；未完成自动模型 adapter、路由引用桥接或 Agent。

`runtime/tests/attachment.c` 从开发 v3 record 包创建两个 Attachment，在调用方释放 Program 引用后验证：

- Host 配置错误发生在 executor 创建后，整个创建失败清理，输出 NULL。
- program/configuration/run/launch/slot/generation 六项分别错配均返回 CONFLICT，frame/result 不变。
- 输入 frame 的 steps/depth 为零且 error 非零，成功调用仍使用 Attachment 配置预算；未知 counter slot 错误后可继续使用。
- 四线程共 40,000 次尝试；成功数可能随调度变化，BUSY 被明确计数为未执行。counter 与所有成功记录数严格相等，另一 generation 的 Host 保持零。
- 测试通过链接器 wrapper 在获准调用进入真实 executor 前暂停它，确定性验证状态读取和并发 run 返回 BUSY，close 后新 run 返回 CLOSED，destroy 返回 BUSY。
- 放行后仍执行真实 VM，旧调用成功完成，最终排空；关闭状态下可读取全部输出，然后销毁。
- 销毁第一个 Attachment 后，第二个独立 Attachment 继续成功执行。

完整 14 项 CTest 在 Clang 23 ASan/UBSan/LeakSanitizer 下通过：[全量日志](c-attachment-1/full-asan-ubsan.log)。追加创建失败清理与确定性 BUSY 用例后，Attachment 测试再次通过：[最终日志](c-attachment-1/asan-ubsan.log)。GCC 16 TSan 验证通过：[日志](c-attachment-1/tsan.log)。两种构建均插桩 C runtime 和 patched uBPF。

复现沿用 Program 测试构建选项，运行 `ctest --test-dir BUILD -R attachment --output-on-failure`。测试 wrapper 仅用于调度屏障，不替代真实 VM/Host 执行；生产库不包含屏障代码。

限制：同 Attachment 串行且竞争时拒绝，尚无上层 BUSY/丢失统计策略；身份为控制方分配而非认证；Host 配置仍显式传入，正式包状态/输出契约未完成。查询只暴露 counter、单 map value 和输出 pop，完整状态快照与 histogram 采集接口仍待控制面接入。
