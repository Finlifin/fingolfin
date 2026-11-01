# 动态对象键与正式 Mount 路由

同一个 C 控制器管理 `FgObjects`、Session 和 39 个具名 adapter。真实 launch begin 分配 epoch；事件按 device/core/kind/local 找到或注册对象，再为 `(ObjectKey, hook)` 建立独立 Mount。每次观察立即排空并逐条校验 `.fg` 输出。launch end 依次 detach、确认 drained、核对计数器、销毁 Mount，然后使该 launch 的对象键失效。

```sh
python3 integration/tt-emule/dynamic-model-runtime/build.py \
  --cb /tmp/fg-cb-model-six-1 --compute /tmp/fg-compute-model-2 --noc /tmp/fg-noc-model-1 \
  --runtime-build /tmp/fg-runtime-pic --ubpf-build /tmp/fg-ubpf-scratch-pic \
  --output /tmp/fg-dynamic-new
python3 integration/tt-emule/dynamic-model-runtime/run.py \
  --artifacts /tmp/fg-dynamic-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build --output /tmp/fg-dynamic-results-new
```

输入为三组已验证的生成 adapter/package（见各组 README）。构建器校验输入哈希、复制 package 到输出目录，并生成只负责帧构造/编码/结果校验的 adapter，不复用各组旧的固定对象控制器。所有 userspace 控制、注册和执行代码为 C；编译器和 `.fg` 保留。

前置条件：真实源码探针、六场景补丁及 `../lifecycle/repeated-scenario.patch`，并应用最新版 `../lifecycle/launch.patch`、重新构建 unit_tests_api。新版 launch 补丁还支持单设备 deferred mesh，在 run_mesh_dispatch 真正执行时建立 scope。已经应用旧版 launch 补丁的工作树应先还原其修改后再应用新版，不可重复叠加。

八个场景验证 39 个 hook，所有核和 CB 均按身份路由，不再仅选择 core0/CB0。DRAM copy 源测试包含六次 launch；重复 NOC 场景执行同一个 Program 两次。各对象/hook 总调用数与独立 trace 比较，且逐个 Mount 核对只属于本批次的计数。退役后尝试调用该路由，要求 CLOSED 且 factory 不执行；新 Mount 上尝试旧键，要求 INVALID 且 factory 不执行。

验证程序含 device0/core0/CB0 的参数条件：不同核或 CB 会影响第三个计数器，但不会阻止 dispatch，C 侧按同样的声明条件核对其结果。CB 输出验证阶段/序号，compute/NOC 还核对当前模型声明的所有采集参数。

当前边界：每进程测试 run ID 固定 71，最多一个活动 launch、512 个活动绑定及 512 条历史绑定记录。首次观察时注册对象，并在首次 hook 事件时创建 Mount（含分配/装载），使用全局 mutex；这是正确性集成，不是热路径性能实现。新版探针 v2 携带 per-fiber device，deferred scope 提供设备集合；双设备本地 CB/状态隔离已经验证（见 ../multidevice）。未知设备或多设备范围内的旧版无设备事件会被拒绝。完整属性 provider、精确底层对象构造销毁、未覆盖的分配/执行失败恢复、并发 launch、通用控制策略和性能验收仍需开发。

## 失败与恢复

当前 launch 的失败标记与累计错误分开：一次装载/参数/容量错误停止该批次后续观测（计入 skipped_events），结束清理后可开启新的 launch；累计错误仍保留，不会把失败历史洗成成功。detach/drain 采用两阶段检查；任何 Mount 未排空都保留绑定和对象键，允许同一 token 在 lease 释放后重试 end。销毁失败同样保留尚未释放的 Mount。历史证据容量独立于活动绑定容量，溢出通过 history_dropped 显式报告。

```sh
python3 integration/tt-emule/dynamic-model-runtime/check-recovery.py   --artifacts /tmp/fg-dynamic-new --runtime-build /tmp/fg-runtime-pic   --ubpf-build /tmp/fg-ubpf-scratch-pic --output /tmp/fg-recovery-check-new
```

四组为合成故障控制器测试：缺失 package、绑定容量耗尽、参数越界、持有 lease 导致 drain BUSY。每组要求错误保留、清理后无活动 launch/绑定、下一批次 generation 增加且计数独立。小容量/持有 lease 接口仅存在于 FG_DYNAMIC_TESTING 测试构建，不进入正式 observer。

真实异常场景需在 repeated-scenario.patch 后应用 `../lifecycle/exception-scenario.patch` 并重新构建 unit_tests_api。kernel 在完成 NOC/CB 数据检查后按 runtime arg 主动抛出异常，宿主 EXPECT_THROW 验证异常，再设置新参数重跑同一个 Program，检查两轮输出和清理。此测试证明正常 C++ 异常展开后的恢复，不覆盖进程 abort、未收到 end、底层分配失败或所有 VM 中途部分提交路径。

当前 Core 按值包含 device:u64。动态 frame factory 从已验证的事件设备 ID 填入该字段；`.fg` 设备条件的实际计数保存在每个 binding 的 matched_context 中。此调用上下文字段不等同于完整对象属性 provider。

## 真实对象树

launch begin 注册根对象；首次有效观测依次注册 device → core → CB/NOC/compute。内部 kind 4/5/6 分别表示 launch 根、设备和核心，1/2/3 保持叶对象类型。父子键属于同一 launch，身份仍在 launch 内唯一。设备及核心按观测惰性注册，未发生事件的设备不出现在树中；这不是完整硬件对象枚举。

对象容量与 Mount 容量分离：一个根、最多 64 个设备，以及每个绑定对应的核心/叶节点最坏上限，另留一次失败事件的空间。每次创建 Mount 及最终清理时从注册表读取父链核验；所有 Mount 排空后 end 原子失效整树，逐个检查保存的祖先键无法解析。BUSY 重试同时保留父链。

JSON 的 ancestors 从近到远为 core、device、launch 根的完整 ObjectKey；tree_checks 计成功退役绑定的父链检查，tree_invalidations 计这些绑定的三条祖先键失效检查（共享祖先会重复计数，并非对象数量）。treecheck.py 额外核对同核心共享、跨设备隔离、无环、同 run/launch 以及对象与身份的一一对应。HTML 展示可展开的真实注册对象树。详见 [验证报告](../../../reports/tt-emule-object-tree.md)。

## 身份属性 provider

`properties.c` 实现 `FgPropertyProvider`，支持从 canonical object 名或根对象字段路径读取 record、根/设备 id、core.x/y 及叶节点 id。每次调用沿 leaf → core → device → root 核验完整存活父链；调用者必须在整个 collect 期间排除模型和注册表变更，动态控制器使用现有 mutex 实现。根 id 是 run，设备 id 保留 u64；NOC/compute id 是当前聚合对象编号，不是硬件通道编号。未知路径、缺失分支、失效键经 facts_collect 转为 unknown；错误/预算耗尽清空整个快照。只读取身份属性，不读取 CB 可变占用量或目标内存。

每个真实 Mount 创建前核对九个属性事实，结束后再次读取并要求全部 unknown。`property_checks` 和 `stale_properties` 分别记录这两类绑定级检查。`check-properties.py` 编译 property-probe.fg 的真实模式及 generated collect/predicate，在 C 对象树上验证设备条件、64 位编号、失效/ABA、错误路径和预算边界。模型显式声明身份属性为 Object 阶段，编译器据此生成 gate；未声明路径仍保守地保留 Invocation 阶段。

## 正式 Mount 的属性策略

构建时由 Rust 编译器为 `property-probe.fg` 生成属性读取与谓词；`build.py --compiler PATH` 可指定编译器，默认使用仓库 target/debug/fingolfin。设置 `FG_DYNAMIC_PROPERTY_GATE=1` 后，CB 的六个 hook 使用构建时选择的策略，默认匹配 device 0、core (0,0)、CB 0；`--property-policy FILE.fg` 可替换 `program::observe` 的 where 条件。其余事件仍进入目标原始 API，但观测 Mount 在 acquire 阶段筛掉，不调用 frame factory 或 BPF。NOC/compute 路由未启用此 CB 策略。

创建 Mount 前按 Object 阶段从 provider 收集完整事实，缺少声明或 unknown 时拒绝创建该策略绑定；Mount 复制带完整身份的不可变 facts。身份属性在对象生命周期内不变，launch teardown 先排空 Mount 再失效对象，避免悬挂快照。配置在注册表创建时从环境读取一次，不支持运行中通过改环境重配。

`multidevice/run.py --property-gate` 对真实双设备的首次/缓存执行核验各 48 个事件：24 执行、24 筛掉，设备 1 无帧构造、输出或计数增长。报告 `filtered` 区分筛选与失败后的 skipped_events。当前支持构建时选择谓词，最多 64 个 Object 阶段读取，尚非运行中热加载/通用残余谓词执行。策略中的函数体不执行，观测主体仍来自原 `.fgo`。设备 1 反向选择可用 `examples/tt_emule/device1_policy.fg` 与 `multidevice/run.py --property-gate --expected-device 1` 验证。
