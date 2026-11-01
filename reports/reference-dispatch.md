# Hook 参考分发验证

日期：2026-11-01。

`Evaluator::dispatch` 与 `dispatch` CLI 已连接模型检查、snapshot 校验、模式求值和入口函数执行。规范 hook 路径决定候选入口；OR 产生去重的绑定行，各行独立执行入口。没有 pattern 的入口执行一次。unknown 导致潜在匹配不确定时，`incomplete` 显式为 true，已知匹配仍保留。

Snapshot 使用规范对象路径和嵌套 Record；缺失字段/Unknown 允许存在，已知字段必须符合模型整数范围、bool、enum、struct 或数组类型，未知字段拒绝。Pointer 值只允许作为已声明指针参数传递，绝不解引用，也不证明地址有效或生命周期正确。

模式求值借用外部 Budget，匹配、所有入口及 helper 调用共享额度；失败前已消耗额度不会退回。参考执行无宿主副作用，失败返回错误，不将其解释为不匹配。

验证：

- `cargo test --offline`：69 个集成测试通过。
- `cargo clippy --offline --all-targets -- -D warnings`：通过。
- `tests/dispatch.rs` 覆盖路由、OR 同行去重/异行执行、未知字段与已知行共存、无效 snapshot/参数拒绝、跨匹配/入口的预算阈值以及指针/数组传值。
- CLI 集成测试执行 `valid_observation.fg` 的 push 入口，绑定 cb_id=16，并调用 increment helper。

原始日志：`reference-dispatch-tests.log`、`reference-dispatch-clippy.log`。

未完成：多值关系和完整模型生命周期、自动属性采集、typed Plan IR、VM 代码生成、真实 cmodel .fg 执行、输出与聚合/有界状态。当前仅提供单值 snapshot 参考路径，不构成 M2 验收。
