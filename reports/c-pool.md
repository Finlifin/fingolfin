# C VM 执行槽池验证

日期：2026-11-01。范围：独立 VM 实例组成的有界执行资源池，尚未接入正式 Attachment/生成 adapter。

`runtime/tests/pool.c` 用 Rust 编译器离线生成的 record ELF，在纯 C 测试进程执行：

- 拒绝 0 / 65 槽和无效 ELF；失败输出为 NULL。
- 两槽同时租用、第三次申请返回 BUSY；重复 acquire/release 及意外复制 token 被拒绝。
- 释放后可重新取得槽位，代际增长；失败的 token 调用不修改 frame/result。
- 关闭幂等，新 acquire 返回 CLOSED；旧租约继续运行；未排空 destroy 返回 BUSY。
- 八线程各执行 5,000 次，分别持有独立 Host；counter 合计和每条输出与线程输入一致。
- 八线程确定性屏障使全部持有租约后主线程关闭；释放屏障后旧租约执行成功，再申请均返回 CLOSED；最终排空并销毁。

Clang 23 ASan/UBSan/LeakSanitizer 与 GCC 16 ThreadSanitizer 均通过。两种构建均对 C runtime 和固定版本 uBPF 插桩。记录：[ASan/UBSan](c-pool-1/asan-ubsan.log)、[TSan](c-pool-1/tsan.log)。本轮加入 pool 后还通过了全套 10 项 CTest；随后增加的关闭屏障用例在两种 sanitizer 下单独重跑通过。

## 复现

按 runtime README 准备 pinned uBPF 和 `record.o` fixture。构建 runtime 时启用 `FG_WITH_UBPF`，传入 `FG_UBPF_ROOT`、`FG_UBPF_BUILD`、`FG_TEST_ELF_DIR`。ASan 构建设置 `FG_SANITIZE=ON`；TSan 使用独立目录，runtime 和 uBPF 都设置 `CMAKE_C_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'`。随后：

```sh
cmake --build /tmp/fg-pool-tsan
ctest --test-dir /tmp/fg-pool-tsan -R pool --output-on-failure
```

约束：同一租约仅由一个执行流使用；Host 的共享同步由调用方负责；destroy 前外部排除所有新旧 API 调用。每槽独立加载代码，尚未共享不可变指令。池 acquire/release 不分配，upstream VM 执行仍会分配；本测试不构成整个执行路径零分配或性能门禁证明。
