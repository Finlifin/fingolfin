# 真实 CB→SDK→uBPF 探针验收

2026-11-01。固定源码和环境版本见 integration/tt-emule/upstream.lock.toml；tt-metal 的 portable SFPI 补丁和 ELFIO stdint 补丁仅用于构建兼容。unit_tests_api 成功构建，原始日志 tt-emule-build-2.log。

## 实际结果

| 真实 gtest 场景 | 模式 | CB 回调 | frame/VM | push/pop 页数 |
|---|---|---:|---:|---:|
| ExactCapacity | on | 1 | 1 / 1 | 2 / 0 |
| ExactCapacity | off | 1 | 0 / 0 | 0 / 0 |
| ExactCapacity | miss（选择 CB16，实际 CB0） | 1 | 0 / 0 | 0 / 0 |
| Wraparound | on | 5 | 5 / 5 | 4 / 2 |
| Wraparound | off | 5 | 0 / 0 | 0 / 0 |
| Wraparound | miss | 5 | 0 / 0 | 0 / 0 |

六组真实 gtest 均通过，无跳过，runtime errors=0。具体 XML、宿主日志、VM 统计和机器可读汇总保存在 tt-emule-cb-probe-1/。预期计数来自固定上游测试源码：ExactCapacity push 2；Wraparound 循环两次 push/pop 1 后再 push 2。

插桩前 baseline-1 的 5 个 HostBufferTest 和上述两个 CB 测试通过；插桩后未加载运行时的 baseline-patched 同一子集也通过。negative-zero 使用不存在的 filter，gtest 自身 exit 0 但 runner 返回 exit 1，证明零测试不会被当成有效基线。

## 路径与边界

cb_api.h 在 push/pop 所有更新后增加可选 weak C symbol 调用，补丁 cb-probe/hooks.patch。JIT DSO 从预加载的单个 runtime.so 解析符号。adapter 构造 SDK 72 字节 frame，safe-profile uBPF 解释执行返回页数，native 收集结果。每个组合使用独立 JIT cache，防止旧 kernel 掩盖新插桩。

这完成了 M0 所需的真实 CB→C ABI→VM 实验证据，**没有完成 M0 全部退出条件，也不是 M2 的 .fg 端到端交付**。BPF 源码为手写探针，不是编译器生成；仅单进程/单 launch，身份 epoch 固定；runtime 使用 mutex 和首次调用初始化，没有热路径性能保证。未验证 NOC/等待观测、正式属性和配置环境、跨 launch 生命周期、动态装卸、任意 DSO 多注册表情况或完整上游回归。LD_PRELOAD 是实验接线方式，正式模型 SDK 仍需显式初始化/函数表绑定。
