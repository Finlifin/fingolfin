# 对象模型：类型、实例、身份、属性和状态

Fingolfin 的对象是观测逻辑中的命名实体。它可以对应设备、核心、CB，也可以是 adapter 定义的聚合实体。对象必须有明确的身份与生命周期；它不必与某个 C++ 类一一对应，更不能仅用一个地址表示。

## 1. 先区分五个概念

| 概念 | CB 示例 | 用途 |
| --- | --- | --- |
| 对象声明 | `tt_emule.device.core.cb` | 定义有哪些属性、关系和 hook |
| 实际实例 | 本次 launch 中 device 1 / core(0,0) / CB0 | 指定观察的实体 |
| 局部身份 | device、kind、core_x/y、local | adapter 在 launch 内查找同一实例 |
| ObjectKey | run、launch、slot、generation | 运行时识别实例并拒绝旧引用 |
| 程序状态 | 该对象/hook 订阅的 counter、map、输出 | 保存观测结果，独立于被观测对象 |

对象声明中的 `id` 是模型属性，不是 ObjectKey.slot。CB 的 id 可以一直是 0，但每次新 launch 的 ObjectKey 都不同。counter slot 0 则只是 Host 配置中的槽号，与 CB0、注册表 slot 都无关。

## 2. 类型路径怎样对应一棵实例树

```text
声明路径                         一次 launch 的实例
 tt_emule                       root（run 71，launch L）
   .device                        device 0             device 1
     .core                         core (0,0)            core (0,0)
       .cb                           CB0                   CB0
       .noc                          aggregate 0           aggregate 0
       .compute                      aggregate 0           aggregate 0
```

声明路径描述关系种类，不是在编译时枚举所有设备。对某个叶对象执行 where 时，provider 以该实例为上下文沿父链解析 `tt_emule.device.id`，得到它所属的设备。当前模式关系是单值嵌套 record；这里的 `.device` 不是让程序遍历所有设备的集合查询。

tt-emule 实例树按事件惰性创建。未出现在树中的核心可能只是没有观测事件；NOC/compute 是每核心聚合节点，不代表已建模底层每个通道或寄存器。

## 3. 为什么不能用宿主指针

宿主分配器可能先在地址 P 创建对象 A，销毁后又在 P 创建 B。仅比较 P 会把 A 的旧记录和状态误归给 B，这就是需要防止的身份复用问题。地址还不能可靠描述设备、批次、父关系或跨进程记录。

ObjectKey 的各字段分工如下：

| 字段 | 变化边界 | 防止的混淆 |
| --- | --- | --- |
| run | registry 的运行域，由宿主保证唯一 | 不同注册表/运行之间碰撞 |
| launch | begin 生成的递增批次编号 | 两轮执行使用相同局部标识 |
| slot | 注册表分配的存储槽 | 定位当前记录，不具有硬件编号语义 |
| generation | 同一槽重新用于新对象 | 旧键误命中新对象 |

四部分必须整体比较。只比较 slot 或只比较 launch 都不足以判断身份。generation 到达上限的槽不再复用；对象 identity 的 device/local 可为零，kind 必须非零。

## 4. 重复、父子和退役规则

局部身份由 device、kind、core_x、core_y、local 组成，在同一 launch 内唯一，**不是仅在父节点下唯一**。两个不同父节点若传入同一局部身份，仍会冲突；adapter 必须设计足够区分实例的身份编码。

register_child 的父对象必须仍存活，并属于同一 run/launch。注册后父关系不可变；子节点只能在创建时附到已有父节点，因而不能通过重挂形成环。若宿主确实要改变归属，应退役旧身份，创建新身份并重建相应绑定，不能悄悄更改已发布父链。

单对象 retire 在仍有子对象时返回 BUSY，应从叶到根退役。launch end 则一次失效整个批次，不要求逐个叶节点删除；其他 launch 不受影响。registry 支持多个 launch 的能力，不代表当前 tt-emule 控制器已支持并发独立 launch。

## 5. 一个对象从出生到销毁

| 时刻 | 对象层 | 路由/程序层 |
| --- | --- | --- |
| 开始批次 | begin 得到新 launch | 尚未发布此批次路由 |
| 识别实例 | 注册根、device、core、叶对象 | 准备入口、Host 和对象 facts |
| 发布 | 键和父链稳定 | Mount 对完整键发布路由 |
| 观察 | adapter 保证目标仍可读 | lease 固定路由，执行程序 |
| 结束 | 暂不复用键/释放目标 | detach，等已有 lease 排空 |
| 回收 | retire 或 end 使旧键失效 | 排空输出、销毁 Mount |
| 下轮 | 相同 CB0 得到新的键 | 新配置拥有独立状态 |

具体实现可调整停止调用来源与 detach 的先后，只要保证目标存储一直活到最后一次使用，并在 destroy 前排除全部访问。不能先 end 然后继续让旧 lease 读取对象属性。

## 6. 三种数据不能互相代替

**属性**是针对对象实例读取的事实，如 device.id、core.x。**调用参数**是某次事件携带的值，如本次 push 的 n 和 phase。**观测状态**是程序累计结果，如进入次数。设备 id 既可以作为身份属性，也可以按值放进 Core 参数，但这两份数据的来源和采集时刻需要 adapter 保证一致。

CB 占用量属于可变目标状态，不应因为它“是对象字段”就承诺整个 Object 生命周期稳定。counter 则是观测程序状态，不自动反映真实 CB 占用。必须明确统计公式和事件边界，才可能从事件推导目标状态。

## 7. provider 与 facts 的关系

provider 在给定对象上下文中解释属性路径，检查叶及全部父链，输出不带借用指针的值。facts 是把一次收集结果复制成的不可变快照，gate 读取快照，避免观察时临时遍历可变模型。

整个 collect 应处于同一模型快照：分别锁住每次读取，却允许两次读取之间换父节点或结束对象，会得到从未真实存在过的组合。缺失/失效属性保留 unknown，不能用 0 代替；0 是合法设备和 CB 编号。收集失败或预算耗尽不发布半份已知事实。

已发布 facts 不能原地修改。可变属性若使用 Update 阶段，需在模型变化前发布失效并排空旧读取，或提供等效安全点/一致快照协议。当前 Mount select 不负责刷新 facts。

## 8. 观测状态如何隔离

StateKey 使用 program/configuration/run/launch/slot/generation。前两部分区分程序与配置，后四部分是对象键。Mount 的每个订阅还拥有独立 Host，因此重复身份并不使两份 Host 自动合并。

在一个既有 Mount 上 select 暂停/恢复订阅会保留其状态；创建新 Mount 则创建新 Host。程序注册项卸载不清除仍被 Mount 保留的状态。报告合并状态时应保留配置和对象上下文，不能仅按 counter slot 聚合。

## 9. 判断模型设计是否足够明确

拿到一条记录，读者应能回答：来自哪个运行和批次、哪个真实实例、哪个 hook/phase、哪份程序配置，是否可能是退役前的在途调用。拿到一个属性，读者应能回答：从哪里读、何时稳定、未知如何表示、何时失效。答不出来的部分应补进模型说明，而不是藏在 adapter 的实现约定中。

API 操作约束见 [注册表参考](runtime-objects.md)，实际设计步骤见 [建模指导](modeling-guide.md)。
