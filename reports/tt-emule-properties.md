# tt-emule 身份属性读取

[打开属性与对象树报告](tt-emule-properties-1/index.html)。C provider 已读取真实注册对象的根/设备 ID、核心坐标、叶对象 ID，以及对应 record 存在性；设备 ID 保留完整 u64。

最终版本在新目录通过完整 17 阶段构建验证。八个真实场景共 15 次 launch、9084 次 dispatch、17256 条输出，39 个观测点全部命中，控制器错误为 0。创建 Mount 时完成 294 次属性快照检查，每次九个事实；launch 结束后完成 294 次旧身份读取，全部为 unknown。双设备首次与缓存执行、四组恢复及四组设备边界测试均通过。

新增定向测试从真实 `.fg` 模式生成 collect/predicate，在 C 注册对象上验证：设备 0 匹配，1、2^32、2^63 不匹配；退役和槽复用后的旧键为 unknown；不存在的分支、标量下继续访问、错误路径长度和不完整父链不会产生已知值；预算耗尽清空整个结果。

[生成谓词测试日志](tt-emule-properties-1/properties/test.log) · [构建阶段](tt-emule-properties-1/stages.json) · [真实结果](tt-emule-properties-1/summary.json) · [源码哈希](tt-emule-properties-1/provenance.json)

本轮实现的是身份属性。根 id 为运行域编号；NOC/compute 的 id 是每核心聚合节点编号，尚非硬件通道标识。可变状态、计算属性、状态维护、兄弟对象查询和完整硬件枚举仍未实现。生成的模式谓词仍在 Invocation 阶段评估，本轮未把它作为正式 Mount 的筛选策略。未进行浏览器截图或整体仿真器 sanitizer 验收。
