# tt-emule 39 点观测与直观测试

[打开最新交互报告](tt-emule-observability-3/index.html)：首页展示六个真实场景的结果、数据流、校验内容、耗时和日志入口；下方提供覆盖地图、API 参数和可筛选事件时间线，无需网络。

已建立 39 个不同 API 观测点：CB 6、NOC 14、compute 19，对锁定源码的 51 处实现插桩。相应 `.fg` 模型声明已通过类型检查。六个真实 gtest 全部通过，实际命中 **39/39** 点，共 **8,890** 条事件，丢弃 **0**；进入/退出不会算成两个观测点。

| 场景 | 实际校验 |
|---|---|
| CB wraparound | 环形缓冲边界与回绕 |
| DRAM reader → compute copy → writer | 原有端到端数据复制校验 |
| NOC / semaphore | 4 个搬运结果、inline write 值 7、信号量值 1 |
| tilize / untilize | 独立检查两张 tile 的中间面布局及还原结果，各 2048 个非均匀元素 |
| multicast | 两个接收核各检查 512 个字，发送核等待双方确认 |
| 算术 / 激活 | 10 种操作各检查 1024 个 BF16 输出元素，exp 使用量化容差 |

新增布局转换和多核 multicast 已通过，39 个点全部命中；不代表全部重载、参数边界或异常路径覆盖。新增场景的一键运行还检查必需 API 是否命中以及线程内 scope 进入/退出配对；测试失败、跳过、零测试、丢弃事件均不会被报告为通过。

历史 `TensixTestCircularBufferWrappingBlockingToCompute` 仍失败：修正 size_t 重载歧义后，继续暴露 `riscv_wait` 缺失、指针参数宽度约束及退出时 double-free。保留 [失败日志](tt-emule-observability-3/legacy-compute-failure/0.log)，未计入成功覆盖，未完成未插桩基线归因。

证据：[参数与源码目录](tt-emule-observability-3/catalog.json)、[插桩补丁](tt-emule-observability-3/hooks.patch)、[实现哈希与版本](tt-emule-observability-3/provenance.json)。[复现说明](../integration/tt-emule/observability/README.md)包含新测试补丁和兼容补丁的应用顺序。旧报告保留在 `tt-emule-observability-1` 与 `tt-emule-observability-2`。

新增报告脚本通过 JavaScript 语法检查；原有筛选功能经过 Node DOM harness 验证。环境没有浏览器，尚未完成浏览器截图验收。39 点采集仍是独立 C 诊断路径，模型生命周期及逐点正式 `.fg` adapter 绑定仍待完成，不能视为完整运行时集成。
