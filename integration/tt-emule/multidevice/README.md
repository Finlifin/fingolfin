# 双设备 deferred 身份验证

探针 v2 从当前 fiber 的 `__emule_self->chip_id` 捕获 device ID，并把它与 core/参数一同传递。RAII scope 保存进入时的身份，退出也使用同一设备；不使用宿主线程 ID 推断设备。C 控制器按 launch 的设备集合验证每条事件，再按 device/core/kind/local 注册对象并创建独立 Mount。

前置补丁：

1. 新 `observability/prepare.py` 自动生成 v2 探针头；已有旧探针使用 `observability/device-context.patch`。
2. 使用最新版 `lifecycle/launch.patch`，在 deferred 实际运行前将完整设备集合交给控制器；还将探针头内容 hash 纳入 JIT cache key，防止 ABI 升级复用旧 DSO。
3. 在原有场景和 exception-scenario.patch 后应用 `multidevice/scenario.patch`，重新构建 unit_tests_api。已应用旧版同名补丁时先还原其修改，再应用新版。

```sh
python3 integration/tt-emule/multidevice/run.py \
  --artifacts /path/to/dynamic-runtime --metal /path/to/tt-metal \
  --build /path/to/tt-metal-build --output /path/to/new-device-results
python3 integration/tt-emule/multidevice/check-boundary.py \
  --artifacts /path/to/dynamic-runtime --output /path/to/new-device-boundaries
```

`run.py` 显式使用锁定源码的 N300 mock topology，要求两个真实模拟设备存在，不能跳过。两个 Program 使用相同 core(0,0)/CB0，填入不同数据并各校验 512 个字；先注册两设备的 fiber，再统一运行，重复两轮。每次 gtest 要求 48 条事件全部进入正式模型、24 个对象/hook Mount 独立退役、12 次旧键路由拒绝、零残留与错误。第二次 gtest 共用 JIT 缓存，要求 DSO 文件内容与修改时间不变。

负向合成测试覆盖：未知设备、在多设备 launch 使用缺少设备信息的 legacy 回调、重复的 launch 设备成员。均要求拒绝且随后有效 launch 可以恢复。旧回调仅在单设备 launch 中可由无歧义范围补齐设备；新版真实探针优先调用 v2。

已验证范围是两个设备同一 deferred 调度、各设备本地 CB 数据和对象状态隔离；不是跨设备 NOC/fabric 传输验收。device ID 同时用于 runtime 身份和 `Core.device:u64` 调用参数；程序条件 `core.device == 0` 在设备 0 命中、设备 1 不命中，C 侧读取并核对实际计数器。完整设备对象属性 provider 仍未实现。并发独立 launch、完整 kernel/fiber 身份、完整参数语义和性能仍待开发。探针头变更进入缓存键，但并未解决所有上游间接头文件变更的缓存追踪。

类型化上下文额外测试使用 0、2^32、2^63 三个合成设备 ID，要求只有 0 命中设备条件，防止适配时截断到 u32。合成 ID 不代表实际模拟器拓扑包含这些设备。模型 Core ABI 更新后需重建全部分组 package/adapter。
