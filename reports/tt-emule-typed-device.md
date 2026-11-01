# `.fg` 类型化设备上下文

[打开设备条件与实际计数报告](tt-emule-typed-device-1/index.html)。模型 Core 增加 `device:u64`，动态 adapter 从已校验的 fiber 设备 ID 填入生成的原生帧，再编码为 BPF 输入。39 个生成程序均使用设备条件，并核对实际 Host 计数。

双设备真实测试中，每个 hook 的 `core.device == 0` 上下文条件在设备 0 命中 2 次、设备 1 命中 0 次，首次与缓存运行一致；两设备仍分别执行与保存数据，未因条件不匹配而停止路由。额外合成测试以 0、2^32、2^63 验证非零高位设备不被截断成 0。

16 阶段全新构建及原有八个真实场景、四组恢复测试通过。Core ABI 已改变，package/adapter 必须重新生成。[可编译示例](../examples/tt_emule/device_watch.fg) 单独完成编译检查，不作为额外实际执行场景计数。

[设备原始结果](tt-emule-typed-device-1/multidevice/summary.json) · [四组设备边界](tt-emule-typed-device-1/multidevice/boundary/summary.json) · [构建阶段](tt-emule-typed-device-1/stages.json)。本轮未进行 sanitizer 或浏览器截图验收。

此项是调用上下文参数，不是完整设备属性 provider。对象父子关系、kernel/fiber 稳定身份、跨设备数据传输、并发 launch、完整参数/重载和性能验收仍待完成。
