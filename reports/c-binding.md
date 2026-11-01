# C 路由到 Attachment 桥接验证

日期：2026-11-01。新增 `binding.h`，测试 `runtime/tests/binding.c` 从 Rust 编译器离线生成的 `.fgo` 出发，执行 SDK lease → Port route → binding encoder → Attachment → C VM → C Host。

验证内容：

- 原生帧大小错误和 encoder 拒绝不产生 Host 效果，SDK report 记录失败。
- 四线程共 4,000 次观测尝试，竞争返回 BUSY；所有成功次数与 counter、每条输出一致。
- generation 1/2 的两个 lease 跨过 generation 3 空路由发布及 Port close，仍正确调用原绑定。
- 当前和退役路由持有 binding 时 destroy 返回 BUSY；错误 expected_generation 发布失败后，候选引用释放，未发布 binding 可销毁。
- Attachment 关闭后仍有 binding pin 时不报告 drained，不能销毁。旧 lease 释放、binding 销毁后才允许清理 Attachment；输出仍可在关闭后取走。

完整 15 项 ASan/UBSan/LeakSanitizer CTest 通过：[全量](c-binding-1/full-asan-ubsan.log)。追加四线程桥接用例后单独重跑通过：[最终桥接](c-binding-1/asan-ubsan.log)。GCC TSan 下桥接与 Attachment 两项通过：[TSan](c-binding-1/tsan.log)。C runtime 与 patched uBPF 均插桩。

复现沿用包含 package/uBPF/scratch 的 C runtime 构建，执行 `ctest --test-dir BUILD -R binding --output-on-failure`。

限制：encoder 是显式 C 回调，尚未由模型编译器生成；gate 仅核对绑定身份/路由代际，尚未执行 pattern Plan。hook ID/name 映射由控制方负责；binding 只服务同步观测，不回填原生函数返回值。此验证不替代真实 cmodel 自动 adapter、完整模型状态契约或发布验收。
