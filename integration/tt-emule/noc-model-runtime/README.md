# 14 个 NOC hook 的正式绑定

每个具名 hook 编译独立 `.fg` 程序，经生成 C adapter、Session/Mount 执行。构建器核对所有源码重载的采集槽与模型声明；缺少参数、槽歧义或位置不一致即失败。`noc_semaphore_wait_min` 的参数来自第二槽，u64 NOC 地址按完整宽度传入。

```sh
python3 integration/tt-emule/noc-model-runtime/build.py \
  --compiler target/debug/fingolfin --runtime-build /tmp/fg-runtime-pic \
  --ubpf-build /tmp/fg-ubpf-scratch-pic \
  --catalog reports/tt-emule-compute-model-runtime-1/catalog.json \
  --output /tmp/fg-noc-new
python3 integration/tt-emule/noc-model-runtime/run.py \
  --artifacts /tmp/fg-noc-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build --output /tmp/fg-noc-results-new
python3 integration/tt-emule/noc-model-runtime/check-boundary.py \
  --artifacts /tmp/fg-noc-new --output /tmp/fg-noc-boundary-new
```

前置条件见 `../observability/README.md`。读写、信号量和 multicast 真实场景验证全部 14 个 hook；运行器独立比对诊断 trace 的调用数。126 个合成边界组合额外覆盖每个 u8 参数的全部 256 种值、u32 上限、u64 高位/最大值、非法 phase、越界与 core 筛选。

每次调用先输出 phase/sequence。u32 参数作为 tag 输出并保留 sequence；u64 地址作为 value 原样输出；u8 参数拆成八个固定顺序 bit 记录，逐条检查。此编码用于验证 DSL 的强类型参数传递，没有隐式整数扩宽。记录值总和按 uint64 取模，仅作冗余检查；正确性以逐条 tag/value 比对为准。

范围限定 core(0,0) 的进程内 NOC fixture；不验证 NOC 地址有效性，只验证声明宽度和参数传输。未采集信号量宿主指针，尚无稳定信号量对象 ID；linked、multicast 目标数量等未声明字段不在此次模型验证中。8192 条记录/每 hook 的验证缓冲有界，溢出计错。跨设备/launch 生命周期、完整参数语义和可配置订阅仍待实现。
