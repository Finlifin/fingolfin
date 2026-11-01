# 保守分阶段门控

日期：2026-11-01。

新增 gate Plan：将 checked pattern 转为 Record/Known/Literal/Range 测试以及 And/Or/Not。绑定值仍由完整匹配器产生；gate 只决定是否可能匹配。共享读取按规范 object anchor 和字段路径去重，阶段由显式 availability 契约提供，默认 Invocation。

三值求值遵守 false ∧ unknown=false、true ∨ unknown=true、not unknown=unknown。只有确定 false 拒绝；未知、过期身份、无效 facts/plan 或预算耗尽返回 allow=true 并提供原因。事实绑定配置代际和 run/launch/slot/generation；显式 invalidate 使对应属性及后代不可用。读字段和 predicate 节点消耗共享预算。

Facts 是调用方提供的有效事实集合；稳定性、失效通知完整性及 invocation 数据新鲜度仍必须由模型 adapter 保证，本库不自动观察宿主变化。route 保留各订阅者判断，只在全部拒绝时关闭，无订阅者直接关闭。状态维护程序后续必须声明自己的调度资格，不能仅依赖用户过滤条件。

计划已加入结构化类型 IR，随开发版 .fgo 保存并由 explain 输出。默认构建不把任何模型属性推测性提前到对象期。

验证：89 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。6 类模式 × 4 类可用性配置 × 25 个完整 snapshot × 4 个阶段，共 2,400 次判断满足真实匹配必通过；Invocation 阶段与参考匹配结果完全一致。另覆盖 unknown、预算、非法 facts、配置和所有 ObjectKey 分量变更、属性失效、共享路由混合资格、序列化与默认阶段。

日志：staged-gates-tests.log、staged-gates-clippy.log。尚未实现 native gate/SDK 路由联动、模型属性契约语法、并发路由发布与回收、完整有界环境和状态；没有将这些测试作为完整 FNG-015–018 或真实热切换门禁。
