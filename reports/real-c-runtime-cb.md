# 真实 tt-emule：生成 .fg 产物与 C 运行时

2026-11-01，两个真实 CB gtest 各运行 on/off/miss，六组全部通过。运行代码位于 [集成目录](../integration/tt-emule/c-runtime/README.md)。

| 场景 | 模式 | 事件 | 构帧 / 分发 / 输出记录 | push 页数 | pop 页数 |
|---|---|---:|---:|---:|---:|
| ExactCapacity | on | 1 | 1 / 1 / 1 | 2 | 0 |
| ExactCapacity | off、miss（各一次） | 1 | 0 / 0 / 0 | 0 | 0 |
| Wraparound | on | 5 | 5 / 5 / 5 | 4 | 2 |
| Wraparound | off、miss（各一次） | 5 | 0 / 0 / 0 | 0 | 0 |

所有运行 errors=0，gtest XML 校验通过。分发次数来自 SDK attempted；成功执行由 C Host 计数器及输出记录核对。原始数据见 [summary](real-c-runtime-cb-1/probe-summary.json)，源码版本和产物哈希见 [provenance](real-c-runtime-cb-1/provenance.json)，动态依赖见 [dependencies](real-c-runtime-cb-1/dependencies.txt)。每组子目录保存独立日志与 XML。

`.fg` 经保留的 Rust 编译器生成包和 C encoder，C loader/Program/Attachment/binding/SDK v2 完成目标调用；目标回调不使用 Rust Host。uBPF 和 C runtime 以 PIC 静态库链接到预加载库。另用 `build.py` 成功重新构建了独立产物，构建日志及哈希保存在本机 `/tmp/fg-real-cb-2`，上述六次运行使用 `/tmp/fg-real-cb-1` 产物。

范围限制：普通函数入口、显式 CB 映射、固定身份、首次事件初始化、全局 mutex。它验证 C 用户态执行链，尚不能代表自动模型绑定、完整生命周期、NOC/等待、Agent/CLI 或性能验收。日志中的可选 Inspector RPC socket 权限错误不影响这些 gtest，但本记录不宣称 RPC 可用。
