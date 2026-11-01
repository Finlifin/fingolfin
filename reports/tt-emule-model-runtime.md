# tt-emule 正式模型绑定：首条链路

`cb_push_back` 现已通过具名 `.fg` hook、生成 C adapter、Session/Mount 和 BPF 程序执行。Observation 改为按值传递；BPF 程序读取元数据与参数，C 侧逐条比对输出的 phase/sequence，检查计数、排空与销毁。

六个真实场景通过，共 668 次选中回调，668 次成功 dispatch，668 条记录，错误 0。仅选择 core(0,0)/CB0，因此这是一个 hook 的正式绑定证据，与 39 点诊断覆盖不同。

| 场景 | 进入 | 退出 | 输出记录 |
|---|---:|---:|---:|
| CB 回绕 | 6 | 6 | 12 |
| DRAM copy | 324 | 324 | 648 |
| NOC / semaphore | 1 | 1 | 2 |
| 算术 / 激活 | 1 | 1 | 2 |
| tilize / untilize | 1 | 1 | 2 |
| multicast | 1 | 1 | 2 |

额外四组独立合成边界测试通过：零/非零 pages 参数分支、忽略未订阅 hook/core/CB、拒绝非法 phase、拒绝超过 u32 的 pages。这些不是实际硬件/API 调用，不计入真实场景覆盖。

[真实结果](tt-emule-model-runtime-1/summary.json) · [边界结果](tt-emule-model-runtime-1/boundary/summary.json) · [生成 adapter](tt-emule-model-runtime-1/push.adapter.h) · [复现步骤与限制](../integration/tt-emule/model-runtime/README.md)。模型/实现哈希与编译产物已随证据保存。

其余 38 个 hook、完整重载语义、跨 launch/device 对象生命周期及 provider 尚未接入。当前对象身份为 fixture 固定值，不能用于一般多设备应用。
