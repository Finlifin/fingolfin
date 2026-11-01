# tt-emule 真实注册对象树

[打开可展开的对象树报告](tt-emule-object-tree-1/index.html)。每个场景和双设备执行结果均展示 launch → device → core → CB/NOC/compute，包含实际注册键的 slot/generation 和叶节点 hook 调用次数。

新建构建目录完成全部 16 阶段验证：39 个观测点全部命中，八个真实场景共 15 次 launch、9084 次 dispatch、17256 条匹配输出，控制器错误为 0。共核验 294 条绑定父链，结束后确认 882 次祖先键失效；共享父节点按绑定重复检查，这两个数字不是独立对象数量。

双设备首次和缓存执行均通过：同核心各 hook 共享父链，不同设备的 core/叶对象隔离；重复 launch 复用槽但 generation 增加。四组故障恢复及四组设备边界检查通过，持有 lease 时保留整个父链，排空后才失效。另对真实报告注入缺失检查、循环、跨 launch、设备身份混用、同核心重复身份五种损坏，检查器全部拒绝。

[构建阶段](tt-emule-object-tree-1/stages.json) · [真实场景数据](tt-emule-object-tree-1/summary.json) · [双设备数据](tt-emule-object-tree-1/multidevice/summary.json) · [恢复测试](tt-emule-object-tree-1/recovery/summary.json) · [源文件哈希](tt-emule-object-tree-1/source-hashes.json)

这是按观测惰性注册的 launch 内对象树，尚非完整硬件对象枚举或属性 provider。kernel/fiber 稳定身份、精确底层对象构造销毁、跨设备传输、并发 launch 和性能验收仍未完成。本轮未进行浏览器截图或集成 sanitizer 验收；此前 C 注册表测试的 sanitizer 结果不代表整个仿真器通过。
