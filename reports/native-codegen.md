# 原生 C11 代码生成验证

日期：2026-11-01。

`src/codegen.rs` 从类型化 IR 生成函数体；`emit-c FILE --model DIR --output FILE.c` 原子写入产物并输出符号映射。使用现有 ABI 类型声明，参数及返回保持原生类型；内部函数使用独立 fg_exec_ 名称，首参 FgExecContext 携带剩余工作量、当前/最大深度及错误码。

错误码为 0 成功、1 预算、2 算术、3 非法值、4 深度。调用时已有错误不会清空，所有返回路径恢复深度。错误时返回零值，调用方必须先检查 error。bool/enum 和结构体内值在参数边界验证；指针只传递，不解引用或验证其生命周期。

表达式依次产生临时变量，保持左到右顺序，短路右支只在需要时执行。加减乘与取负使用 Clang overflow builtin；除法检查零及 signed-min/-1，余数 signed-min/-1 显式产生 0，避免 C UB。执行预算与参考执行器各自计费，二者步数不保证相等。

验证：

- `cargo test --offline`：79 个集成测试通过。
- Clippy warnings-as-errors、cargo fmt --check：通过。
- 生成代码使用 clang -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all 编译执行。
- 496 组分支、调用、短路及 u8 乘法结果/失败判定与参考执行器一致。
- u64 极值乘法、i64 极值除法/取负/余数、除零、预算耗尽、调用深度和恢复通过。
- struct 按值返回、非法 bool/enum、opaque pointer、带模式绑定的入口及 helper 真实执行通过。
- CLI 生成测试夹具 .fg 的原生目标文件；直接数组 ABI 明确拒绝。

原始日志：`native-codegen-tests.log`、`native-codegen-clippy.log`。`native-codegen-targeted.log` 保留初次定向验证。

限制：这是原生 C11 后端，不是 eBPF。入口绑定是按 IR bindings 名称排序的附加参数，宿主须先做模式匹配；未生成 dispatcher、adapter/frame、输出状态接口或 .fgo。直接数组表达式暂要求结构体包装。真实 cmodel 上的 .fg→VM 运行及正式 M2 门禁仍未完成。
