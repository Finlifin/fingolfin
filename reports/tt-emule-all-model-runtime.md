# 39 个 hook 的统一 C 运行时验证

[打开正式执行对照表](tt-emule-all-model-runtime-1/index.html)。六个真实场景通过，39 个 hook 在同一个 runtime.so 中均有执行证据，共 5796 次成功 dispatch、13044 条逐条核对的输出，错误 0。调用数、每个 adapter 收到的事件总数与同版本源码上的独立诊断运行一致。

本轮补齐 14 个 NOC hook，另有 [126 个合成边界组合](tt-emule-noc-model-runtime-1/boundary/summary.json) 通过：u8 全部 256 种取值、u32 上限、u64 高位和最大值、越界、阶段及 core 筛选。合成值只验证传输宽度，不表示这些地址在实际 NOC 中有效。

统一运行时链接前校验输入对象、package、adapter 哈希；六个 CB、19 个 compute、14 个 NOC adapter 各自拥有 Mount 和状态。CB 核对元数据及参数条件；compute/NOC 还逐个输出已声明参数并比对。

[复现说明](../integration/tt-emule/all-model-runtime/README.md) · [原始结果](tt-emule-all-model-runtime-1/summary.json) · [独立诊断时间线](tt-emule-all-model-runtime-1/reference/index.html)。静态对照表尚未进行浏览器截图验收。

当前范围仍是进程内 core0 fixture（CB 限定 CB0）。完整 API 参数/重载、跨 launch/device 对象身份、生命周期/provider、动态订阅以及性能验收仍未完成。39/39 hook 执行不能作为整个项目完成的证明。
