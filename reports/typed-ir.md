# 结构化类型 IR

日期：2026-11-01。

实现 `src/ir.rs` 与 `ir FILE --model DIR [--output FILE]`。输入为 CheckedProgram；普通函数及入口均转换为带源码位置和类型的结构化表达式/语句。所有函数引用解析为规范符号，局部变量分配函数内唯一 u32 ID；内层 initializer 在引入新 local 前解析，保证遮蔽语义。字段访问包含已检查布局的字段偏移。逻辑与/或保留独立短路节点；负整数字面量正确保留 i64 最小值。

类型检查额外保留每个入口的模式绑定类型，降低时分配不可变 local。pattern 当前保留 AST，不声称完整 Plan IR 或门控优化已完成。IR version=1 是开发格式，尚未提供持久化加载、版本迁移或不可信 IR 验证。

验证：`cargo test --offline` 74 个集成测试通过，Clippy warnings-as-errors 和 cargo fmt --check 通过。新增测试覆盖遮蔽 initializer、分支独立编号、外层赋值、规范调用、短路结构、最小整数、嵌套字段偏移、入口绑定和确定性序列化；CLI 输出也已测试。

日志：`typed-ir-tests.log`、`typed-ir-clippy.log`。下一阶段仍须实现实际后端执行与参考差分、完整模式/门控 Plan IR 以及优化正确性验证。
