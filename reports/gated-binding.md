# C gate 接入 binding 验证

日期：2026-11-01。新增版本化 `FgBindingGate` 与 `fg_binding_create_gated`，把 plan/facts 复制为 binding 所有的不可变快照。沿用现有 route 引用与 Attachment pin 生命周期。

`runtime/tests/gated_binding.c` 使用编译器生成的 record adapter，经过真实 SDK/route/Attachment/C VM 验证八种情况：已知 false、true、unknown、过期完整身份、Invocation 谓词未就绪、零预算、缺失 predicate、非法 topology。仅已知 false 跳过 factory 和 dispatch；其余保守放行，counter 与成功调用数一致。

创建后立即覆盖调用方的 plan/facts，测试仍按原快照求值。generation 2 的已取得 lease 跨过后续发布和 Port close 执行，binding 保活到 release。创建阶段未使用 Rust runtime；Rust 只离线生成包/adapter。

完整 18 项 Clang ASan/UBSan/LeakSanitizer CTest 通过：[日志](gated-binding-1/asan-ubsan.log)。GCC TSan 下 gated_binding 和已有四线程 binding 测试通过：[日志](gated-binding-1/tsan.log)。新 gated_binding 用例是确定性的代际场景；不能将其描述为已覆盖所有并发 facts 更新场景。

复现使用包含 package/uBPF/generated adapter 的 runtime 构建；`ctest --test-dir BUILD -R gated_binding --output-on-failure`。

未完成项：Plan 的编译器/C 表达转换、模型属性与字面量谓词、外部属性失效通知、真实 cmodel 自动门控。快照要求平坦值且 predicate 只读，未提供指针图深拷贝。
