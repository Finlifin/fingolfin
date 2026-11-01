# 双设备身份传递与 deferred 路由

[打开统一验证报告](tt-emule-multidevice-1/index.html)。全新目录完成 16 个阶段：原有八个真实场景与四组恢复测试通过，新增双设备首次执行/缓存复用测试及三组设备身份边界测试通过。

真实 N300 mock topology 上，两设备在一个 deferred 调度中使用相同 core(0,0)/CB0，各写入并校验 512 个不同数据字。每次 gtest 重复两轮，共 48 次正式调用，24 个独立 Mount 退役，12 次旧键路由拒绝，错误与残留均为 0。两个设备的对象键不相同，第二轮 generation 递增。

设备 ID 来自 fiber 的 chip_id，scope 进入/退出使用保存的身份。控制器验证它属于当前 launch 的设备集合；未知设备、重复成员以及多设备范围内的 legacy 无设备事件都被拒绝，下一次有效执行可恢复。

探针头内容已加入 JIT cache key。升级验证从旧的 2 个 DSO 新增 2 个新键 DSO；随后缓存运行的文件哈希与修改时间均不变。[升级证据](tt-emule-multidevice-1/cache-upgrade.json) · [新目录缓存证据](tt-emule-multidevice-1/multidevice/cache.json)。该验证不覆盖所有上游间接头文件的缓存失效。

[复现说明](../integration/tt-emule/multidevice/README.md) · [16 阶段记录](tt-emule-multidevice-1/stages.json) · [双设备原始结果](tt-emule-multidevice-1/multidevice/summary.json)。本轮未做浏览器截图或 sanitizer 验收。

尚未完成跨设备 NOC/fabric 数据传输、并发独立 launch、完整 kernel/fiber 身份与模型属性 provider、全部参数/重载、未覆盖的失败恢复和性能验收。
