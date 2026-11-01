# 普通函数参考执行

日期：2026-11-01。

`src/sema.rs` 保存每个函数的表达式类型，以 start:end 字节区间为键；`src/eval.rs` 使用这些检查结果执行函数，不重新推断类型。此次新增 `eval` CLI，可从 `.fg` 源文件实际执行普通函数。

支持整数、bool、enum、固定字段 struct，普通函数调用、嵌套分支、局部变量和赋值、提前返回、字段读取与短路逻辑。参数和结果按声明类型检查；数值中间结果按表达式位宽检查，拒绝越界而不静默截断。共享 Budget 对函数、语句、表达式和值验证收费；独立深度限制约束调用栈。

实测命令：

```sh
cargo run --offline -- eval tests/fixtures/valid_observation.fg --model models/fixture --function program::increment --args '[{"Integer":41}]'
```

结果为 `{"Integer":42}`，工作量 10 步。步数是当前参考实现计费，不代表 CPU 指令数或 VM 指令数。

验证：`cargo test --offline` 共 63 个集成测试通过；Clippy warnings-as-errors 通过。测试覆盖短路跳过除零、失败路径报错、u8/i8 溢出、i64 最小值、嵌套作用域/赋值/提前返回、结构体和枚举边界、预算精确阈值、调用深度和 CLI 错误退出。日志：`reference-eval-tests.log`、`reference-eval-clippy.log`。

未完成：hook 的 snapshot/pattern 分发、指针和数组值、输出/聚合/有界状态接口、typed Plan IR、后端差分与实际 VM 代码生成。此结果不构成完整 FNG-010 或 M2 验收。
