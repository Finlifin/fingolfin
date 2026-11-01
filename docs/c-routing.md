# C 路由与 SDK v2

实现：[route.h](../runtime/include/fingolfin/route.h)、[fingolfin_v2.h](../sdk/include/fingolfin_v2.h)。适用于当前 Linux x86-64 little-endian C ABI。SDK v1 保持原样；SDK v2 增加 lease 协议，调用帧继续使用 `FgCallHeader` 的 ABI version 1。

## 端口与配置发布

`fg_port_create(hook, object, max_subscribers, allocator, out)` 创建一个 hook/完整 ObjectKey 的端口。初始代际为 0、路由为空。每端口最多 64 个订阅者，支持配置更小的容量；重复订阅者 ID 拒绝。

`fg_port_publish(port, expected_generation, subscribers, count, published_generation)` 先构造候选不可变路由，再在短锁内检查并发布。成功增加 generation；过时代际返回 CONFLICT，关闭后返回 CLOSED，分配/容量/代际溢出错误不会替换现有路由。输出 generation 仅成功时写入。发布可能分配内存，不属于零分配观测路径。

`FgSubscription` 保存 context、gate、dispatch 和可选 retain/release。retain 与 release 必须同时提供或同时为空；提供时，每个候选路由持有一次引用，发布失败或最后一个使用者退出后释放。两者为空表示借用 context，调用方必须保证它活到路由和所有旧 lease 退出。所有用户回调与资源释放都在发布锁之外，可以重入发布/查询。

事实和配置必须与该订阅代际保持一致。示例不代表可以在并发执行中原地修改 context 内的事实。共享可变状态需要单独同步；代码与事实版本不应跟随“当前全局配置”漂移。

## 同一次 lease 覆盖整个观测

```text
fg_v2_acquire → gate / 固定选择结果 → 构造 frame → fg_v2_dispatch → fg_v2_release
```

`fg_port_runtime(port)` 返回借用端口的 `FgRuntimeV2` 函数表。端口必须覆盖整个表的使用期。`fg_v2_acquire` 获取线性 `FgLeaseV2` token，固定 route/code/context/generation；不分配堆内存。不得复制、伪造、原地修改或并发使用 token，且不能用一个未释放的 token 再次 acquire。

只有 gate 返回 0 才表示已知拒绝；NULL 或其他值保守通过。当前 gate 是 C 回调契约，完整模型 Plan 的自动 lowering 尚未接入。即使所有订阅者都拒绝，成功 acquire 仍必须 release。

`fg_v2_observe` 提供常用封装：仅在 enabled 时调用一次 factory，factory 接收 lease 的确切 generation 和 object。随后检查通用调用头并 dispatch，最后释放 lease。factory 返回的存储必须活到 observe 返回；回调不能以异常或 longjmp 跳过释放。factory/dispatch 失败也会释放 lease。

SDK 检查 frame 的 ABI、最小长度、hook、完整 ObjectKey 和配置代际。具体 adapter 仍须检查精确 frame 大小和 payload 类型。native frame 不能直接视为开发 BPF frame；测试中 adapter 显式转换，正式生成 adapter 待实现。

一次 lease 最多执行一次 dispatch。订阅者按发布顺序执行；错误不会阻止同次其他订阅者。`FgDispatchReportV2` 报告 attempted/failed/first_error，dispatch 返回 first_error。需要逐订阅者详情时应扩展预分配输出，当前未实现该记录协议。

## 关闭、排空与销毁

`fg_port_close` 禁止新 acquire，卸下当前路由；旧 lease 仍可按其原代际执行。关闭幂等。`fg_port_closed_and_drained` 检查全部历史代际 lease，以及发布/关闭时尚未完成的旧资源释放；它不是“只看最近一次 retired”。

`fg_port_destroy` 要求 closed+drained，否则返回 BUSY。除此之外，调用方必须排除所有其他 API 使用者，包括进行中的发布、回调和将来可能使用旧 runtime 表的线程；不要从这些调用内部销毁端口。对象模型资源只能在所有旧回调不会再访问后释放。

实现使用每端口 pthread mutex 和原子 route 引用计数，是正确性基线，不是无锁或 epoch 实现。借用 context、对象生命周期通知和外部线程停止仍属于集成方责任。

## 验证与 VM 接入边界

纯 C route 测试验证发布在 gate/frame 之间发生、所有历史代际排空、析构后再报告 drain、回调/析构重入、拒绝不构帧、错误继续分发、64 订阅者选择位图、分配失败保持旧路由、10,000 次观测不调用 allocator，以及 10,000 次发布与四线程共 40,000 次观测。GCC ThreadSanitizer 检查通过；C/C++ 调用兼容测试通过。

启用 uBPF 的测试已连接 SDK v2 → C route → fixture adapter → C VM → C Host，验证拒绝路径不构帧/不更新状态，允许路径更新计数并输出记录。该 adapter 为测试实现，不代表真实 cmodel 自动适配完成。

同一 VM 的并发占用返回 BUSY；Host 不隐式加锁。当前路由还没有自动分配 per-worker VM/Host 执行资源，生产接入需要明确共享/分片策略。VM safe interpreter 内部逐次分配仍待解决。详细证据见 [路由验证](../reports/c-route.md)。
