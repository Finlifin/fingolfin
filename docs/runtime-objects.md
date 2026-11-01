# C 对象身份注册表

`runtime/include/fingolfin/objects.h` 提供有界、线程安全的对象身份分配。创建时分配全部存储，后续 begin/register/find/resolve/retire/end 不分配堆内存。它是模型生命周期的身份层，不持有目标内存，也不替代 route lease 或 Mount 排空。

- 创建 registry 时指定非零 run ID、并发 launch 上限和对象槽上限。调用者保证不同 registry 生命周期域的 run ID 不重复。
- begin 生成严格递增的非零 launch ID；结束的 launch ID 不复用。并发 launch 可以注册同样的局部对象标识。
- 对象标识由 device、kind、core_x/core_y 和 local ID 组成。kind 非零，其他字段允许零。字段是 adapter 分配的稳定编号，不以宿主指针作为公开身份。
- 同一 launch 内重复 register 返回 CONFLICT；find 返回现有键。不同设备、核心、类型或局部编号属于不同对象。
- register_child 只接受同一 run、同一活动 launch 的存活父对象；父关系不可修改，对象身份仍在 launch 内唯一。parent 返回父键快照，根对象或失效键返回 NOT_FOUND。
- retire 在仍有存活子对象时返回 BUSY；应先退役叶节点，再退役父节点。成功后使当前键失效。复用该槽时 generation 增加；达到上限的槽不再复用，防止 ABA。
- end 原子地使该 launch 全部对象键失效，不影响其他 launch。后续旧键 resolve/retire 失败。
- destroy 在仍有活动 launch 时返回 BUSY；真正销毁要求调用者先阻止并发 API 调用。

所有方法用一个 mutex 保护表状态；查找为有界线性扫描，不作为高频观测路径性能方案。失败不修改输出参数。返回的是身份快照，不保证 resolve 返回后目标仍存活；真实 adapter 需在执行边界同步注册/失效，并在目标销毁前排空相关 lease。

测试覆盖容量与输出不变、跨 launch 同号对象、跨设备身份、槽复用旧键拒绝、结束一个 launch 不影响另一个、重复注册竞争，以及两个线程合计 4000 次注册/查询/退役。`integration/tt-emule/lifecycle` 已在真实非 deferred launch 边界验证身份注册/失效；动态 Mount 路由与双设备 mesh 已有独立集成验证，见 `reports/tt-emule-typed-device.md`。注册表父子关系已接入真实 tt-emule adapter，见 [对象树验证](../reports/tt-emule-object-tree.md)；完整属性 provider 仍待完成。

父子关系新增测试覆盖三级树、跨 launch/run 拒绝、重复身份、叶节点优先退役、父槽复用后旧键拒绝、整树失效与其他 launch 隔离。普通构建和 ASan/UBSan/LeakSanitizer 构建的 C 测试均为 9/9 通过；sanitizer 在沙箱外运行，以避开 ptrace 限制。
