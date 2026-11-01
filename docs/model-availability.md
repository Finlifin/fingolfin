# 模型属性的可用阶段

模型包可在 `Fingolfin.toml` 中声明精确属性路径的阶段：

```toml
[[availability]]
object = "tt_emule"
fields = ["device", "id"]
stage = "Object"
```

stage 使用 `Attachment`、`Object`、`Update` 或 `Invocation`。这是受信任模型对 provider 的契约，不是编译器从属性名推测的优化。Object 表示该对象身份生命周期内可使用稳定快照；Update 需要宿主实现完整的失效与重新发布协议。声明不会自动实现 provider、缓存或失效通知。

每个声明只匹配一个精确的 object/fields 组合。空 fields 表示对象 record 本身；声明 record 的阶段不改变后代字段阶段。未声明路径保持 Invocation。不同 anchor 写法不自动展开别名，模型应为实际使用的路径分别提供契约。

编译观测程序时检查对象与每段字段/子对象是否存在，拒绝重复契约、对象关系与字段歧义、穿过标量继续访问以及越界路径。清单解析拒绝未知阶段和未知配置字段。IR 与生成 C adapter 的 reads 和 gate 节点均采用这些阶段，C Mount 不接受超过 Update 的提前筛选阶段。

非空契约按对象、字段路径和阶段排序后参与模型内容哈希。重排相同声明不改变哈希；新增或修改声明会改变模型身份，需要重新生成程序与 adapter。没有声明的旧模型保持原内容哈希算法和保守阶段。

tt-emule 当前只为已实现的身份属性声明 Object：注册对象不可重挂父节点，设备/核心/局部编号不变，结束时先排空 Mount，再失效整个 launch。可变占用量、执行状态等未因此获得缓存许可。

验证命令：`cargo test --test source`。真实提前筛选见 [tt-emule Mount 属性策略](../reports/tt-emule-property-gate.md)。
