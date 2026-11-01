# 具名 tt-emule hook 到 C 运行时

此集成将真实 `cb_push_back` scope 回调转换为 `tt_emule.device.core.cb.cb_push_back` 的类型化调用帧，执行 `push.fg`，而不是把参数交给未绑定模型的通用函数。链路为：源码探针 → C factory → 编译器生成 adapter → Session/Mount → BPF 程序 → counters/records。

`Observation` 在模型中按值传入，避免把宿主地址当作可供 BPF 解引用的上下文。程序读取 phase/thread/sequence、Core 坐标以及 CB 参数；每条输出的 phase 和 sequence 都与 C 侧独立保存的原始回调逐条比对。六个真实场景另外检查固定的预期调用数，防止完全不调用却通过。

```sh
python3 integration/tt-emule/model-runtime/build.py \
  --compiler target/debug/fingolfin --runtime-build /tmp/fg-runtime-pic \
  --ubpf-build /tmp/fg-ubpf-scratch-pic --output /tmp/fg-model-runtime-new
python3 integration/tt-emule/model-runtime/run.py \
  --artifacts /tmp/fg-model-runtime-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build --output /tmp/fg-model-results-new
```

前置条件：PIC C agent/runtime 和带 scratch 支持的 uBPF 已构建；六个场景及探针按 `../observability/README.md` 准备。不要同时 preload 诊断 `trace.so`，两者实现相同探针回调符号。输出目录必须为新目录。

当前明确范围：一个进程中的 core(0,0)/CB0，hook ID 2；只连接这一个具名 hook。对象键使用此 fixture 的固定身份，没有建立跨 launch/device 的生命周期注册。sequence/thread 在当前 adapter 进程内生成；scope 退出不代表 API 成功。每进程最多保存 1024 条选中事件，越界、非法 phase 或 u32 截断风险记为错误。析构时 detach、确认 drained、检查每条记录并销毁 mount；Session 在装载后已销毁，用于验证 mount 独立持有程序。

模型的 Observation ABI 从指针改为值后，旧 adapter/package 必须重建。39 点诊断覆盖与本目录的一个正式模型绑定是不同进度，不能互相替代。尚需扩展所有 hook、重载语义、对象注册/provider、跨 launch 身份、可配置订阅与动态控制。
