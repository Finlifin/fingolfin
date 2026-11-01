# 19 个 compute hook 的正式绑定

为模型中的每个 compute hook 编译独立具名 `.fg` 程序、生成 C adapter，并通过 Session/Mount 执行。运行时选择 core(0,0)，使用进程内 compute fixture 身份。寄存器、算术、pack/copy、布局转换和激活操作都已在真实测试中命中。

每个程序输出 phase/sequence，随后把每个已声明 u32 参数作为记录 tag 输出（value 保留 sequence），避免依赖隐式整数扩宽。C 侧逐条核对全部 tag/value、计数和队列排空。无参数、一参数、二参数、三参数使用各自生成的帧；例如 leaky_relu 的 slope 来自第三槽，不是第二槽。

```sh
python3 integration/tt-emule/compute-model-runtime/build.py \
  --compiler target/debug/fingolfin --runtime-build /tmp/fg-runtime-pic \
  --ubpf-build /tmp/fg-ubpf-scratch-pic \
  --catalog reports/tt-emule-compute-model-runtime-1/catalog.json \
  --output /tmp/fg-compute-new
python3 integration/tt-emule/compute-model-runtime/run.py \
  --artifacts /tmp/fg-compute-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build --output /tmp/fg-compute-results-new
python3 integration/tt-emule/compute-model-runtime/check-boundary.py \
  --artifacts /tmp/fg-compute-new --output /tmp/fg-compute-boundary-new
```

前置条件与 CB 集成相同：PIC C runtime/agent、支持 scratch 的 uBPF、真实探针和场景测试。已有旧探针还需向 tt-emule 应用 `../observability/tilize-count.patch`；新的 prepare.py 已包含该修正，不要重复应用。修正五参数 tilize_block 的 num_tiles 采集，旧 catalog 的该槽为空，构建器会拒绝。上例保存的 catalog 是原 catalog 加这一明确修正；新生成 catalog 也可用。

构建器要求所有重载都能在同一槽找到模型声明的参数（ntiles/num_tiles 作为明确别名）；字段缺失、歧义或位置不一致直接报错。并非所有 API 参数都在当前模型中：例如 binary 操作的 itile0/itile1、exp 的 vector_mode/scale 尚未建模；pack 的额外 offset 也未声明。此集成验证模型当前声明的字段，不代表完整 API 参数或全部重载已经覆盖。五参数 tilize 重载的采集修正尚无独立真实场景覆盖。

运行器将三个真实场景的调用数与历史独立诊断 trace 比对，并要求 19 个 hook 的命中并集完整。额外 114 个合成组合验证值传递、字段槽选择、u32 边界、非法阶段和 core 过滤；不将合成输入计为真实 API 覆盖。

每个 hook 最多保存 8192 条输出记录，超过容量计错；一个事件产生 1+字段数 条记录。19 个 adapter 各自管理 Mount、输出与线程编号；dispatcher 给每个 adapter 递交事件以保留进程序号，不是热路径性能实现。不能与同名探针回调的其他 preload 同时启用。跨 launch/device 对象注册、生命周期/provider、完整重载语义和通用控制面尚待实现。
