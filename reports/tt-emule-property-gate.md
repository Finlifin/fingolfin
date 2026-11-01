# tt-emule 正式 Mount 属性筛选

[打开执行／筛掉对照报告](tt-emule-property-gate-1/index.html)。模型清单声明身份属性在 Object 阶段稳定，Rust 编译器为 `.fg` 模式生成读取和谓词，C provider 从真实对象树收集事实，正式 Mount 复制快照并在 acquire 时筛选。

真实双设备测试开启 `FG_DYNAMIC_PROPERTY_GATE=1`，对六个 CB hook 使用 device 0、core (0,0)、CB 0 策略。首次和缓存复跑各有 48 个事件：设备 0 的 24 个执行并产生 24 条输出；设备 1 的 24 个被筛掉，frame factory 未调用、无 BPF 执行或计数增长。两台设备仍完成原始计算和数据输出校验，控制器错误为 0。

最终新目录完成全部 18 阶段验证，包含原有 39 点覆盖、八个真实场景、属性读取、失败恢复、未筛选双设备对照、属性筛选及设备边界。`cargo test --quiet` 全部通过；新增测试覆盖声明验证、精确路径不继承、阶段降低、清单排序与模型哈希。

[筛选原始结果](tt-emule-property-gate-1/property-gate/summary.json) · [未筛选对照](tt-emule-property-gate-1/multidevice/summary.json) · [构建阶段](tt-emule-property-gate-1/stages.json) · [编译器源码哈希](tt-emule-property-gate-1/compiler-source-hashes.json) · [契约说明](../docs/model-availability.md)

策略当前从构建时的 `property-probe.fg` 生成，由受信任的 C 控制器用于 CB Mount；不等同于通用动态用户策略加载。未知事实会阻止创建此开发策略绑定，尚未实现通用 unknown 残余谓词执行。可变状态属性、运行中策略更新、跨设备传输、并发 launch 和性能验收仍待完成。本轮无浏览器截图或整体仿真器 sanitizer 验收。
