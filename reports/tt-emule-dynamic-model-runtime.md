# 动态对象身份与正式 Mount 联合验证

[打开逐 launch / 对象键报告](tt-emule-dynamic-model-runtime-1/index.html)。七个真实场景、13 次 launch 通过，39 个 hook 均接入动态键；共 8988 次成功 dispatch、16772 条匹配输出，错误 0。

验证 149 次旧键不能调用新 Mount，256 次 detach 后不能调用原 Mount，均检查 frame factory 未执行。每个对象/hook 的计数独立核对，输出立即排空，launch 结束后销毁 Mount 并使身份失效。

本轮修正了单设备 deferred mesh 的 scope 边界；DRAM copy 源测试实际执行六轮 launch，验证器按源码真实次数检查。第一次未接 deferred 的运行与第二次仍假设单次 launch 的验证失败均未作为成功证据。

[复现与限制](../integration/tt-emule/dynamic-model-runtime/README.md) · [原始结果](tt-emule-dynamic-model-runtime-1/summary.json) · [实际 launch 补丁](tt-emule-dynamic-model-runtime-1/launch.patch)。静态 HTML 尚无浏览器截图验收。

仍未完成：多设备 mesh/per-fiber 身份、完整 API 参数与重载、父子关系/provider、精确底层对象生命周期、异常/容量耗尽后的恢复、并发 launch、通用控制策略和性能验收。
