# C VM 预分配 scratch 验证

日期：2026-11-01。固定 uBPF revision：`8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521`。附加源码补丁位于 `integration/ubpf/patches/safe-scratch-v1.patch`，独立准备脚本 `prepare-scratch.py`。这是本项目补丁，未声称 upstream 已合并。

## 实现边界

补丁提取 safe interpreter 共用执行主体，保留原始 `ubpf_exec_ex_safe` 接口及分配行为；新增 opaque scratch create/destroy/exec API。新接口只接受 safe profile，使用固定大小的预创建 stack/shadow/spill metadata，执行前清零。执行预算、指令处理、helper、指针标签检查共用原逻辑。C VM 在 load 阶段拥有 scratch，以原 busy 标志保证独占；pool 每槽独立 scratch。C VM 额外显式开启未初始化栈/寄存器检查。

启用条件：`FG_WITH_UBPF=ON`、`FG_UBPF_SCRATCH=ON`，使用打补丁后的 headers 和 library。不启用 scratch 时仍能构建兼容 upstream，但不具备执行零分配结论。不能同时在多个调用中使用裸 upstream scratch 指针；新增 upstream API 自身不提供锁。

## 证据

- 完整 12 项 CTest 在 Clang 23 ASan/UBSan/LeakSanitizer 下通过，C runtime 和 uBPF 都插桩：[日志](c-scratch-1/asan-ubsan.log)。
- GCC 16 ThreadSanitizer 下 scratch、vm_alloc 和 pool 三项通过，含八线程 40,000 次 VM 执行与关闭排空：[日志](c-scratch-1/tsan.log)。
- `vm_alloc` 在加载 VM/Host 后启用链接器 wrapper，覆盖 malloc/calloc/realloc/aligned_alloc/free。20,000 次调用含成功、helper 槽不存在、程序步数预算、错误帧长度及 VM 指令预算耗尽，结果检查通过，分配和释放均为 0。
- 使用相同 C runtime 和同一测试、关闭 scratch 并链接未打补丁 upstream，编译测试时定义 `FG_EXPECT_ALLOCATIONS=1`；对照测得 40,000 次分配及释放：[日志](c-scratch-1/baseline.log)。因此计数器并非始终报告零。
- `scratch` 对旧执行接口和新 scratch 接口作 2,000 次交替对照：一次写栈并成功返回 7，下一次跳过写入后读同一位置必须失败。两者判定一致，确认清零不遗留已初始化标记。

复现命令见 runtime README 的 scratch 小节。sanitizer 构建需让 uBPF 和 runtime 使用相同插桩类型；TSan 与 ASan 使用不同构建目录。

计数只覆盖所链接 C runtime/uBPF 的这些分配 API，不覆盖共享库内部、任意 callback 或所有未来程序。该证据不替代完整语言差分、真实 cmodel 性能门禁、正式 Attachment/adapter 与发布验收。
