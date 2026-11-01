# tt-emule 模型开发版

按锁定 tt-emule `07f1bd8301544403c8bc1faa4038f6cbf69909f1` 的真实 API 提供 39 个具名 hook：CB 6、NOC 14、compute 19。类型化参数使用源码中对应宽度；Core 包含 device:u64 和坐标，Observation 按值传递，包含 sequence/thread/phase（0 进入、1 离开，不表示成功）。源码对应与重载参数差异见 `reports/tt-emule-observability-1/catalog.json`。

模型对象树：tt_emule → device → core → cb/noc/compute。动态 C adapter 已按 launch 注册根、设备、核心及观测到的叶对象，使用 run/launch/slot/generation 隔离身份；设备与核心按观测惰性建立，尚非完整硬件枚举。身份属性 provider 可读取根/设备 id、核心 x/y 和叶对象 id，失效或缺失路径返回 unknown。

诊断插桩位于 `integration/tt-emule/observability`，能输出真实 API 事件，39 个 hook 已在 CB/compute/NOC 三组 adapter 中接入正式生成 adapter、Session/Mount 和 BPF 程序，并通过 `integration/tt-emule/all-model-runtime` 在同一个 runtime.so 中验证（限定 core0 fixture）。当前只覆盖模型已声明字段，完整 API 参数和对象生命周期仍未完成。每个声明采用首个源码重载的命名参数；适配其他重载时必须处理有符号/缺省/未采集字段，不能直接将原始三个槽无条件当作完整类型化事件。完整正式模型需补其余绑定及生命周期契约。Observation 已从宿主指针改为值，旧生成 adapter/package 必须重建，不能直接复用。

动态对象键及正式 Mount 联合验证见 `integration/tt-emule/dynamic-model-runtime`：支持普通与单设备 deferred launch、不同核心与 CB、结束时 detach/drain/destroy 和旧键拒绝。旧固定对象集成保留为历史基线；run ID 仍为测试域，注册发生在首次观测，完整设备/fiber/底层对象生命周期及可变状态属性仍待完成。

双设备 deferred 身份隔离已在 `integration/tt-emule/multidevice` 验证：探针 v2 从 fiber 捕获 chip_id，运行时按设备集合校验并隔离同 core/CB 的 Mount。设备 ID 现已作为 `Core.device:u64` 按值传给 `.fg` 并验证设备条件；设备身份属性现已接入 C facts，完整设备状态属性仍未实现，跨设备数据传输和并发 launch 仍未验收。

Core ABI 已增加 device 字段，必须重新生成 package 和 adapter；旧缓存/旧 `.fgo` 不应与新帧布局混用。动态 adapter 从实际事件设备填充该字段，分组历史 fixture 则固定为设备 0。

对象根和设备的 id 声明为 u64，保留高位设备编号。根 id 是测试运行域的 run ID（当前为 71）；CB id 是被观测的 CB 编号；NOC/compute 目前每核心各一个聚合节点，其 id 为 0，不表示已识别硬件 NOC 通道或计算引擎。provider 沿真实父链核验身份，仅支持当前观测路径，不能访问未枚举的兄弟对象。

[身份属性验证报告](../../reports/tt-emule-properties.md) 包含真实场景的存活/失效属性快照检查，以及 `.fg` 模式生成谓词对 C provider 的 true/false/unknown 验证。身份属性现在通过 Fingolfin.toml 的精确 availability 声明为 Object 阶段；未声明的路径仍为 Invocation。正式 Mount 可启用 CB 身份属性策略，见 [筛选验证](../../reports/tt-emule-property-gate.md)。
