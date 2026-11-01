# 实例路由代际与 lease 回收基线

日期：2026-11-01。

新增 src/runtime.rs。每个 Port 固定完整 ObjectKey 和订阅者上限；publish 对 expected generation 做条件更新，拒绝冲突、重复 subscriber ID、容量超限、关闭后更新和代际溢出。新路由包含配置 stamp、facts、gate 和 Arc-owned callback，作为不可变单元发布。

acquire 在端口自己的短 RwLock 内克隆 Arc 并登记活跃 lease。observe 在锁外判定各订阅者；全拒绝不构造 frame，有资格时只构造一次并调用独立回调。一次 lease 使用同一代际，允许 callback 重入/发布，回调错误保留为各订阅者执行结果。

替换操作在锁外释放旧路由。Retired::is_drained 只检查对应代际；Port::is_closed_and_drained 统计所有旧 lease，避免最近代际已释放但更早调用仍在途的问题。lease 先释放 route，再降低活跃计数，使排空信号不早于资源析构。共享 callback 的其他持有者仍可延长代码寿命。模型资源必须由宿主在对象关闭并完整排空后释放。

验证：94 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。新增测试覆盖关闭路径 frame 为零、混合资格构造一次、callback 错误可见、旧代码在 lease 持有期间不析构、跨旧代际关闭排空、重入发布、重复/超限和两个 writer 冲突。压力测试进行 10,000 次更新，4 个并发 reader 共 40,000 次调用，验证回调捕获的代际、frame 和 stamp 始终一致。

原始日志：route-lifecycle-tests.log、route-lifecycle-clippy.log。

限制：这是 Rust RwLock/Arc 正确性基线，尚非 epoch/无锁实现；未连接 C SDK、实际 VM/code loader 或 cmodel 的对象销毁通知。压力测试不是完整项目配置/路由切换验收，尚未运行针对此实现的并发检测器、ASan 或完整真实工作负载。状态维护订阅者与可靠失效通知仍须后续接入。
