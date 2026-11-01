# 动态 Mount 失败恢复验证

[打开真实异常与合成故障报告](tt-emule-dynamic-model-runtime-2/index.html)。八个真实场景、15 次 launch 通过，共 9,084 次成功 dispatch、17,256 条匹配输出，控制器错误 0；全部结束后活动 launch、未释放 Mount、丢弃记录均为 0。

修复两类缺陷：累计错误不再永久阻止后续 launch；清理失败时保留 Mount 与对象键，直到 lease 排空后重试结束。当前批次失败后停止后续观察，并显式记录 skipped_events；累计错误仍保留。

四组合成故障通过：缺失 package、绑定容量耗尽、参数越界、held lease 导致 BUSY。每组保留一个预期错误，并验证下一批次使用新 generation、独立状态及成功清理。真实 exception 场景在 kernel 完成数据校验后抛出异常，宿主确认异常，再运行同一个 Program，验证 RAII 清理和重跑。

[原始真实结果](tt-emule-dynamic-model-runtime-2/summary.json) · [合成故障结果](tt-emule-dynamic-model-runtime-2/recovery/summary.json) · [复现说明](../integration/tt-emule/dynamic-model-runtime/README.md)。本轮未作 sanitizer 或浏览器截图验收。

未覆盖进程 abort、未收到 end、全部分配失败/VM 部分提交的恢复。多设备 mesh、完整参数与生命周期/provider、并发 launch 和性能验收仍待完成。
