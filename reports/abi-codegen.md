# 模型原生 ABI 生成验证

日期：2026-11-01。范围：Linux x86-64 小端原生 ABI；对应 FNG-011 的类型/声明生成子集。

实现位于 `src/abi.rs`，CLI 为 `abi --model DIR --output FILE [--rust]`。声明检查后生成 C/C++ 头文件或 Rust repr(C) 模块，以及规范符号映射。生成器保持字段顺序，处理嵌套结构、数组字段、递归指针、固定整数 enum 与函数/hook 原型，包含布局静态断言。直接数组参数和返回类型明确拒绝。

验证：

- `cargo test --offline`：51 个集成测试通过，0 失败。
- `cargo clippy --offline --all-targets -- -D warnings`：通过。
- `tests/abi_codegen.rs`：C 与 C++ 编译、Rust 绑定编译、生成代码实际链接并双向调用按值结构体；覆盖整数 enum 边界、字段顺序、名称编码与确定性输出。
- `tests/cli.rs`：CLI 生成的 C/Rust 文件分别由 clang/rustc 检查。

原始结果：`abi-codegen-tests.log`、`abi-codegen-clippy.log`。

此验证不证明 VM ABI、hook adapter 或完整观察程序执行正确。当前输出只有声明，无函数实现、frame 生成和生命周期验证；模型源码哈希也不是 ABI 兼容性哈希。完整开发目标保持未完成。
