# 六个 CB hook 的正式模型绑定

从 `models/tt_emule` 编译六个具名程序，每个生成独立 C adapter，分别连接 reserve/push/wait/pop/get_write_ptr/get_read_ptr。前四个使用 pages 参数，后两个使用只有 CB ID 的帧；不将未采集的返回指针伪装为参数。C 运行时共用 `../model-runtime/runtime.c`，每个 adapter 有独立 Mount、计数器及输出队列。

```sh
python3 integration/tt-emule/cb-model-runtime/build.py \
  --compiler target/debug/fingolfin --runtime-build /tmp/fg-runtime-pic \
  --ubpf-build /tmp/fg-ubpf-scratch-pic --output /tmp/fg-cb-model-new
python3 integration/tt-emule/cb-model-runtime/run.py \
  --artifacts /tmp/fg-cb-model-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build --output /tmp/fg-cb-results-new
python3 integration/tt-emule/cb-model-runtime/check-boundary.py \
  --artifacts /tmp/fg-cb-model-new --output /tmp/fg-cb-boundary-new
```

使用带探针及四份场景补丁的测试构建（见 `../observability/README.md`）。新版 NOC 场景增加入队后的 get_read_ptr 调用和数据检查；已有旧测试源码需同步该变更并重新编译。四份补丁按顺序应用，已验证可重放为实际测试源码。

六场景 × 六 hook 的固定调用数检查、BPF 计数器检查和每条输出 phase/sequence 核对均必须通过。零调用组合明确保留，不计为该场景命中；get_read_ptr 由 NOC 场景实际命中。24 个合成边界组合覆盖 pages 取零/非零、非法 phase、超过 u32 的 pages、未选中的核和 CB；指针查询不消费第二槽，因此该槽即使超过 u32 也不应拒绝。

范围仍是单进程 core(0,0)/CB0 fixture。六个程序订阅同一 fixture 对象键，按 hook 分别路由；不存在跨 launch/device 的真实身份注册。dispatcher 将事件转交六个 adapter，使各自记录同一进程序号顺序；这是用于正确性验证的实现，不宣称热路径零开销。各 hook 最多保留 1024 条选中事件，溢出计错。六个独立 adapter 各自给线程编号，编号只在该 adapter 内有意义。

不要与实现同名回调的 trace.so 或旧 runtime.so 同时 preload。这里证明六个 CB hook 的正式绑定；其余 NOC/compute 的 33 个点仍是诊断路径，重载完整语义、生命周期/provider 和通用动态订阅仍需开发。
