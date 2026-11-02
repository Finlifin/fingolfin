# 观测指导：从问题到可解释的结果

一次有效观测需要先定义统计对象、事件边界和结果含义，再选择程序和容量，最后用独立证据核对。程序能编译或报告有曲线，只能说明部分流程工作，不能证明统计回答了原始问题。

## 1. 把问题改写为可验证的口径

问题：“设备 1 的 CB0 使用了多少次？”仍有歧义：哪个核心、哪个 launch，reserve 还是 push，进入还是成功完成，重复执行是否累计？先写成：

> 在每个 launch 内，对设备 1 的各核心 CB0，统计 cb_push_back 的 phase=0 事件数；按对象/hook 分开保存，并记录每次命中的设备 ID。不把进入次数解释为成功提交次数。

这个定义明确了分组、触发和边界。若要统计提交页数，应另定义 n 是否代表请求量或实际量，类型是否可累加，以及失败时是否计入。不要先写代码再让读者猜 counter 的含义。

## 2. 选择正确的观测层

| 想回答的问题 | 所需数据/机制 | 现有示例能否直接回答 |
| --- | --- | --- |
| 某 API 进入多少次 | hook + phase + counter | 可以 |
| 哪个设备/核心触发 | 完整对象上下文、Core 参数 | 可以 |
| 两个对象状态是否混在一起 | 独立 Host 和完整键 | 可以验证 |
| 调用是否成功 | 明确结果或目标数据校验 | 不能仅凭 exit 判断 |
| 实际传了多少字节 | 单位、请求/完成语义、相关参数 | 需按具体 hook 核对 |
| 某次调用真实耗时 | 成对事件、可靠时钟和匹配规则 | 诊断 trace 可辅助，不能直接当性能验收 |
| 硬件当前占用状态 | 可变属性 provider/一致快照 | 当前身份 provider 不提供 |

优先选最接近问题语义的事件，避免从许多间接事件反推一个未被验证的指标。

## 3. 一个完整的计数程序

下面与现有 device_watch 示例采用同一签名：

```fg
use tt_emule.types.{Observation, Core};

fn watch on tt_emule.device.core.cb.cb_push_back(
    call: Observation, core: Core, cb_id: u32, n: u32
) {
    if core.device == 1 && cb_id == 0 && call.phase == 0 {
        builtin.counter_add(0, 1);
        builtin.emit(call.phase, core.device);
    }
}
```

counter 0 统计进入次数，emit 的 tag 是 phase，value 是设备 ID；n 本例不参与统计。每个匹配事件贡献一次 `+1` 和一条 `(0,1)` 记录。此程序未限定核心，因此不同核心的 CB0 都满足参数条件，结果仍按各对象 Mount 分开保存。

检查与生成现成文件：

```sh
cargo build --locked
target/debug/fingolfin check examples/tt_emule/device_watch.fg --model models/tt_emule
target/debug/fingolfin build examples/tt_emule/device_watch.fg \
  --model models/tt_emule --function program::watch --output /tmp/fg-watch-guide.fgo
target/debug/fingolfin emit-adapter examples/tt_emule/device_watch.fg \
  --model models/tt_emule --function program::watch --output /tmp/fg-watch-guide.adapter.h
```

这些命令生成产物，不把它自动替换进统一验证脚本的 39 个程序。实际挂载要由模型控制器选择此入口、对应 adapter 和 Host。先学习 [可运行 C 教程](c-walkthrough.md) 的加载/挂载闭环；当前 tt-emule 构建时可替换的 property-policy 只替换谓词，不替换全部观测主体。

## 4. 同时定义 Host 和输出契约

本程序需要 counter slot 0 初值 0、输出队列和 helper 1/2 权限。假设每个对象在消费者排空前最多出现 100 次匹配事件，则每个对应 Host 至少需 100 条队列容量；这是估算前提，不是系统默认保证。

若只关心总数，可删除 emit，减少输出压力；若需要逐事件证据，就必须安排排空频率、容量和失败处理。队列满会使 emit 失败，但之前 counter_add 已生效。因此只有在所有执行成功、记录都已排空且没有丢弃时，counter 增量才应等于输出条数。

Host 的 FgRecord 只有 tag/value，不自动携带对象键、函数名和时间。消费者必须在读取时附加 Mount 的 program/configuration、run/launch、ObjectKey 和 hook 上下文，再做跨对象聚合。不能把多个队列的 `(0,1)` 混在一起后再尝试恢复来源。

## 5. 选择 if 还是 where

主体 if 读取本次 frame 参数。在上述程序中，设备 0 的事件仍可能构帧和执行函数，只是没有 helper 副作用。观察到 counter 为零不能推断该对象没有执行开销。

where 读取对象属性；在可信 availability 和 provider 完整时，可产生提前 gate，避免不匹配对象构帧。设备 1 的策略例子是 `where tt_emule: { device: { id: 1 } }`。它与 `core.device == 1` 数值上应一致，但数据来源和求值时机不同。

tt-emule 的 `--property-policy` 用策略入口的 where 生成谓词，**不执行其函数体**；被挂载的计数逻辑仍来自各 hook 包。修改 policy 的 body 不会改变正式观测记录。其策略准备要求 Object facts 完整且真值已知；NOC/compute 不应用该 CB 策略。

需要本次调用参数才能确定的条件留在主体。可变状态若没有一致快照和失效协议，不应为了减少开销而强行声明成早阶段属性。

## 6. 设计 counter、map 和 histogram

| 需求 | 建议方式 | 需要写明的口径 |
| --- | --- | --- |
| 总事件数 | counter_add | phase、是否只统计成功执行 |
| 按有限标量分类 | map_add | key 编码、最多不同键数、容量满时策略 |
| 样本分布 | histogram_add | sample 单位、边界、weight 含义 |
| 保留逐次证据 | emit | tag 字典、value 编码、上下文与排空策略 |

例如 histogram bounds `[0,10]` 分出 `[-∞,0)`、`[0,10)`、`[10,+∞)`。weight=2 表示该桶增加 2，不是样本值乘 2，也不是自动记录两条明细。map 的容量约束不同 key 数，满时仍可更新已有 key。

不要用宿主地址作持久 map key，也不要把多个字段随意拼接进 u64 而不说明位宽和碰撞条件。跨对象统计优先在消费者明确聚合；当前不同 Mount 的 Host 不自动共享状态。

## 7. 用一个预期表先验收语义

对上述程序，给出这些输入时应得到：

| device / cb / phase | 是否进入 helper | counter 增量 | emit |
| --- | --- | --- | --- |
| 1 / 0 / 0 | 是 | 1 | tag=0,value=1 |
| 1 / 0 / 1 | 否 | 0 | 无 |
| 0 / 0 / 0 | 否 | 0 | 无 |
| 1 / 2 / 0 | 否 | 0 | 无 |

这是程序主体的预期，不是通用 gate 的 acquire 行为。再在真实场景检查每一类至少有一个样本，避免测试全是正例。新增核心、设备、重复 launch 后，验证各自状态隔离；结束后旧键不得再次构帧。

## 8. 分层验证，不让统计自证

先用编译器检查类型和模型契约；普通函数可用 eval 确认计算和 helper 状态。然后在 C 路径检查 frame、权限、预算和 Host。最后让真实目标产生已知数据，同时核对独立采集、正式 dispatch 和程序记录。

例如 multicast 不能只看“两个接收核都有事件”；还要检查各接收核的数据。布局转换不能只看“来回还原”；还需独立检查中间 tile 布局，防止两个错误相互抵消。观测校验也不能完全复用同一份错误采集值来算期望，至少有目标数据或独立 trace 作对照。

现有场景的具体断言与局限见 [报告指南](tt-emule-reports.md)。

## 9. 结果为空或数量不符时

依次区分：目标未运行、hook 未触发、身份不匹配、gate filtered、factory 未执行、执行预算/Host 失败、body 条件未命中、记录已被消费。每种情况对应不同修复，不能统称“程序没生效”。

进入/离开各计一次会自然翻倍；转发重载可能嵌套；一个宿主测试可能包含多个 launch；一个 dispatch 可以 emit 多条；多个订阅各自记账也会增加记录总数。先按对象、hook、phase、程序配置分组，再比较预期。

并发执行出现 BUSY 应记录而不是悄悄丢弃。filtered 与失败后的 skipped_events 分开，程序错误与目标计算错误分开。报告里应展示失败和遗漏，不能只展示成功记录。

## 10. 每份观测结果应附带的说明

记录问题定义、模型/程序版本、predicate 与 body、对象分组、单位和 phase、Host 容量/预算、运行场景、错误/过滤/丢弃计数、清理是否成功、输入与产物身份。明确结论适用于哪些输入，哪些参数/异常/并发路径未覆盖。

一次结果可以证明“这些场景中设备隔离及计数符合预期”，不自动证明所有设备、所有 API 参数或性能目标。操作命令见 [tt-emule 指南](tt-emule.md)，出错定位见 [排错手册](troubleshooting.md)。
