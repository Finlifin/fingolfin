# C 三值门控核心验证

日期：2026-11-01。新增 `gate.h`/`gate.c`，默认 C runtime 构建即可使用，无 json-c/uBPF 依赖。

`runtime/tests/gate.c` 检查：完整三值 AND/OR 真值表和 NOT；阶段未就绪不调用 callback；预算不足保守放行；callback 增加预算、非法 truth 或失败状态保守放行；前向/循环索引拒绝且不触发 callback；过期对象 generation 放行；1,024 节点 NOT 链通过且少一步预算放行。

Clang ASan/UBSan/LeakSanitizer：[日志](c-gate-1/asan-ubsan.log)。GCC Release：[日志](c-gate-1/gcc.log)。复现：默认 runtime CMake 构建，运行 `ctest --test-dir BUILD -R gate --output-on-failure`。

这验证 C 逻辑核心和保守错误处理，不是 `.fg` Plan→C 的完整语义差分。TEST 的属性/字面量语义由未来模型 adapter 提供；未接入 binding。当前后序全量求值不短路，预算或无关节点错误可能造成额外放行；不允许将此结果描述为完整门控优化完成。
