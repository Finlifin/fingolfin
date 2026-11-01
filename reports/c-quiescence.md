# Port 可恢复排空验证

日期：2026-11-01。新增 quiesce/ticket、暂停状态下发布、resume 接口。发布从分配/retain 前即占 active，提交失败后的 release 与旧路由析构也在排空计数内。

`runtime/tests/quiescence.c` 验证：

- generation 1、2 的历史 lease 都阻止排空；暂停后新 acquire 返回 BUSY 且 factory 未调用。
- 旧 lease 在暂停时仍可执行；只释放其中一个不足以排空。
- 未排空时暂停发布被拒绝；没有发布新代际不能 resume；错误 generation/ticket 不能操作。
- 发布 generation 3 后恢复，新 acquire 使用新订阅；新暂停周期拒绝旧 ticket。
- 以原子屏障把候选 publish 阻塞在 retain 内，随后 quiesce 不得报告已排空；候选恢复后提交被拒绝，release 结束才可报告排空。
- 暂停中 close 后不能 resume，终止排空仍可销毁。

完整 20 项 ASan/UBSan/LeakSanitizer CTest 通过：[日志](c-quiescence-1/asan-ubsan.log)。GCC TSan 下 quiescence、route（已有多线程发布/观测）、binding 与 gated_binding 四项通过：[日志](c-quiescence-1/tsan.log)。

GCC 验证还发现生成头文件的紧凑 if/return 和 pointer sizeof memcpy 引发 warnings-as-errors，已改成显式块和命名字节长度。adapter_codegen 测试现同时用 Clang/GCC 编译所有类型样例，两个 Rust 测试与 Clippy 通过。

复现：runtime 构建后运行 `ctest --test-dir BUILD -R 'quiescence|route' --output-on-failure`。TSan 对 C runtime 使用独立插桩构建。

范围：控制端必须串行拥有 ticket，宿主必须自行提供模型安全点。此 API 不停止模型执行，也不保存暂停期间被 BUSY 拒绝的观测；尚未接入真实模拟器更新协议或无损采集统计。
