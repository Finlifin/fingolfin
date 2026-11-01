# 39 个 hook 共用一个 C 观测运行时

将 CB、compute、NOC 三组已生成的 adapter 对象链接为一个 `runtime.so`，由同一个源码回调分发到 39 个具名 `.fg` 程序。输入对象、package、adapter 的 SHA-256 必须与各组构建清单一致，否则拒绝链接。三个分组构建说明分别位于 `../cb-model-runtime`、`../compute-model-runtime`、`../noc-model-runtime`。

```sh
python3 integration/tt-emule/all-model-runtime/build.py \
  --cb /tmp/fg-cb-model-six-1 --compute /tmp/fg-compute-model-2 --noc /tmp/fg-noc-model-1 \
  --runtime-build /tmp/fg-runtime-pic --ubpf-build /tmp/fg-ubpf-scratch-pic \
  --output /tmp/fg-all-new
python3 integration/tt-emule/all-model-runtime/run.py \
  --artifacts /tmp/fg-all-new --metal /tmp/fingolfin-tt-metal \
  --build /tmp/fingolfin-emule-build \
  --reference reports/tt-emule-all-model-runtime-1/reference \
  --output /tmp/fg-all-results-new
```

替换输入为本机三个分组构建输出。bundle manifest 引用原 package 的绝对路径，运行期间必须保留这些输入目录。六场景的 hook 调用数与独立诊断 trace 比较；同时检查每个 adapter 接收到的总事件数、BPF 计数器、每条输出、队列排空和 Mount 销毁。要求 39 个 hook 的真实命中并集完整，不能以零调用通过充当覆盖。

六个场景需使用包含 NOC get_read_ptr 数据校验的最新版场景补丁，且使用修正 tilize num_tiles 采集的探针。保存的 reference 为该源码上的独立诊断运行。不要同时 preload 其他实现 fg_tt_scope_event 的共享库。

当前是单进程 fixture 集成：CB 选择 core0/CB0，compute/NOC 选择 core0；39 个 adapter 分别拥有 Mount/状态，尚未合并为通用对象注册系统。dispatcher 为保持每个 adapter 的进程序号，向所有 adapter 递交每个事件；不作为热路径性能实现。完整参数/重载、跨 launch/device 身份、生命周期/provider 和动态控制仍需开发。39/39 表示当前具名 hook 均能执行，不代表整个项目完成。
