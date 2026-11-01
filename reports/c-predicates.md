# C 具体谓词与生成验证

日期：2026-11-01。新增 `facts.h`/`facts.c`；编译器 adapter 生成 `fg_generated_predicate`，CTest 元数据增加原始 `read_index`。实现 Record/Known、四种 literal 类型及整数范围，事实槽不含宿主指针。

验证：

- C facts 测试覆盖 signed i128 最小/最大、u64 最大与 -1 的区分、开闭端点；bool/string/variant 比较；含 NUL 的 UTF-8 文本、非法 UTF-8/bool、unknown、预算、非对齐读取与截断缓冲。
- `tests/gate_codegen.rs` 新增模型模式差分：真实 `root` 对象/hook，AND、OR、NOT、整数 literal 与 inclusive range 三个模式，各 64 组已知/未知事实，共 192 组。运行编译器生成的 C predicate 和 C gate，对比 Rust Plan::evaluate，clang UBSan 下全部一致。
- 原有 756 组逻辑差分继续通过。`cargo test --offline --test gate_codegen` 与 Clippy all-targets warnings-as-errors 通过；adapter 类型生成测试也已通过。
- 完整 19 项 C ASan/UBSan/LeakSanitizer CTest 通过：[全量日志](c-predicates-1/full-asan-ubsan.log)。补充 bool/variant literal 后单独 facts 测试通过：[最终 facts](c-predicates-1/asan-ubsan.log)。

复现：Rust 上述 gate_codegen 测试；C runtime 默认构建后 `ctest --test-dir BUILD -R facts --output-on-failure`。生成 header 仍需与当前 runtime 头文件/库配套。

范围：facts 是本机固定布局数组，文本上限 256 字节，总 snapshot 上限沿用 64 KiB；长值必须变为 UNKNOWN，不能截断制造已知值。尚未自动采集属性、验证父路径关系、发布失效通知或接入真实 cmodel 生命周期。不构成完整自动模型门控完成。
