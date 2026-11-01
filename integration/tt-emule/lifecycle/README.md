# 真实 launch 身份与槽复用验证

`launch.patch` 修改 tt-metal 的真实 `launch_cores`：非 deferred 路径在 `run_until_idle()` 前建立 RAII scope；单设备 deferred 路径在 `run_mesh_dispatch` 真正运行时建立 scope，正常退出或异常展开时结束。begin 接收实际 device ID，C observer 调用 `FgObjects` 分配 launch；源码 API 事件依据 device/core/kind/local 注册对象，end 使整批键失效。

```sh
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/lifecycle/launch.patch
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/lifecycle/repeated-scenario.patch
cmake --build /tmp/fingolfin-emule-build --target unit_tests_api -j2
python3 integration/tt-emule/lifecycle/run.py \
  --metal /tmp/fingolfin-tt-metal --build /tmp/fingolfin-emule-build \
  --output /tmp/fg-lifecycle-new
```

前置条件：已有 `../observability` 探针及四份场景补丁；上述补丁仅应用一次。重复场景把同一个 Program 在同一进程中执行两次，每次检查 NOC 数据、信号量和 CB 数据。运行器另执行算术与多核 multicast，检查 compute/CB 分类和三个核心的独立身份。

报告保存每次观察到的对象键及描述。重复场景要求两个 launch、96 条事件、同槽 generation 递增、8 次旧键拒绝检查。其他场景要求实际 core 和对象种类集合正确。对象 kind：1=CB、2=NOC 观测对象、3=compute；CB 的 local 是 CB ID，NOC/compute 当前每核心只有一个观测对象，local=0 不表示底层 channel 或 kernel ID。

`FG_LIFECYCLE_RUN` 为调用者提供的非零 run ID，测试固定 71，仅在该测试进程内使用；它不是跨进程的全局唯一编号。表及证据记录有界，最多 256 条对象创建记录。重叠 launch、无活动 launch 的事件或容量耗尽计错，避免误归属。

范围与后续工作：当前支持非 deferred 和单设备 deferred launch；多设备 mesh 的 v2 身份已接入 dynamic-model-runtime，本目录旧 observer 仍仅支持单设备。异常展开使用 RAII，但进程 abort 无法保证 end；正常 C++ 异常展开后的同 Program 重跑已由 dynamic-model-runtime 的 exception-scenario 验证，进程 abort 不在该范围。注册点是首次观测，不是每个底层对象构造函数，暂无父子关系/provider。本目录仍是独立的生命周期 observer（同名回调不可同时 preload）；动态身份与正式 Mount 路由的联合实现见 `../dynamic-model-runtime`。
