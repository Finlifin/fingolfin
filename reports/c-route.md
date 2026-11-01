# C 路由与 SDK v2 验证

日期：2026-11-01。

新增独立 C route 模块及 SDK v2 header。每端口绑定 hook 与完整 ObjectKey，条件发布不可变代际；线性 lease 持有同代际 gate 选择、context 与代码。C SDK factory 在 gate 允许时只构造一次 frame，头部验证包含配置代际。订阅者错误继续分发并汇总报告。关闭禁止新 acquire，跨历史代际排空后才允许销毁。

实现为每端口 pthread mutex + 原子 route 引用计数；用户 gate、dispatch、retain/release 和资源释放都不持发布锁。drain 计数包含在途 lease 与旧路由回收，资源析构结束后才减计数。

验证：

- route 测试：跨代际一致、拒绝不构帧、重入发布/查询、旧代际保活、关闭后排空、错误报告、64 订阅者边界、失败发布保持旧配置。
- 10,000 次发布、4 个读线程共 40,000 次观测；GCC 16.2.1 ThreadSanitizer 通过，未报告数据竞争。
- 10,000 次稳定路由观测未增加 allocator 调用数。
- 启用 uBPF 的 4 个 CTest 程序通过；runtime 与 uBPF 同时启用 ASan/UBSan，包含 LeakSanitizer 的沙箱外复验通过。
- GCC Release 默认 C 构建 3 个 CTest 程序通过（最终 64 订阅者用例也在 GCC TSan 构建验证）。
- Clang++17 warnings-as-errors 的模型侧 C++ 调用测试完成 acquire/dispatch/release，并调用 C 库销毁端口；修正了首轮 C++ 对 `{0}` 聚合初始化的告警。
- C VM 测试增加 SDK v2 → route → fixture adapter → generated BPF → C Host，拒绝不构帧，允许时记录正确。

证据在 c-route-1/：普通/ASan/TSan CTest 日志及编译 flags、C++ 命令。复现文档为 docs/c-routing.md。

限制：模型 Plan 自动 gate、生成 adapter、per-worker VM/Host 执行资源、fiber 场景、正式包 loader、Agent/CLI、真实 cmodel 接入仍待实现。没有宣称无锁、完整运行时零分配或全部生命周期契约完成。token 不可复制，port destroy 仍要求外部排除全部 API 使用者。
