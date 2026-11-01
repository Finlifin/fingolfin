# tt-emule 使用指南

## 观测范围

模型包含 39 个 API 观测点：CB 6、NOC 14、compute 19，对应 51 处实现插桩。entry/exit 是同一观测点的两个阶段，exit 不表示操作成功。模型与 hook 名称见 [模型目录](../models/tt_emule/README.md)。

控制器注册 launch → device → core → CB/NOC/compute 对象树。Core.device 为 u64；根 ID 是运行域编号，NOC/compute 当前是每核心聚合节点，不能解读为已识别的硬件通道。

## 准备环境

需要锁定的 tt-metal、tt-emule、uBPF checkout，以及已配置的 tt-metal CMake 构建目录。外部源码不随仓库提交。先按 [探针与场景准备](../integration/tt-emule/observability/README.md) 安装插桩和场景，再应用 [launch 生命周期](../integration/tt-emule/lifecycle/README.md) 与 [双设备场景](../integration/tt-emule/multidevice/README.md)。版本、补丁顺序和输入校验集中见 [完整验证说明](../integration/tt-emule/VERIFY.md)。

## 完整重建与验证

```sh
cargo build --locked
python3 integration/tt-emule/verify.py \
  --compiler target/debug/fingolfin \
  --metal /path/to/tt-metal --emule /path/to/tt-emule \
  --metal-build /path/to/tt-metal-build \
  --ubpf-source /path/to/patched-ubpf \
  --output /tmp/fg-verification-new --jobs 2
```

输出目录必须不存在。验证从源码重新构建 C runtime 和 39 个 adapter，执行独立诊断、真实场景、属性与故障恢复、双设备及策略筛选。失败时保留日志，不把宿主测试通过误认为 adapter 验证通过。

打开输出中的 `results/index.html` 查看结果；`stages.json` 记录各阶段命令、退出码和耗时，`provenance.json` 记录源码和关键产物哈希。

## 选择 CB 属性策略

动态 adapter 构建器接受 `--property-policy FILE.fg`，默认使用 `property-probe.fg`。策略文件提供 `program::observe` 的 where 条件；这里只使用生成的属性谓词，观测主体仍来自各 hook 的 `.fgo`。参考 [设备 1 策略](../examples/tt_emule/device1_policy.fg)。

```sh
python3 integration/tt-emule/dynamic-model-runtime/build.py \
  --compiler target/debug/fingolfin \
  --property-policy examples/tt_emule/device1_policy.fg \
  --cb /path/to/verification/cb --compute /path/to/verification/compute \
  --noc /path/to/verification/noc \
  --runtime-build /path/to/verification/runtime-build \
  --ubpf-build /path/to/verification/ubpf-build \
  --output /tmp/fg-device1-new
```

运行时设置 `FG_DYNAMIC_PROPERTY_GATE=1` 启用 CB 策略。未启用时保留全部观测；NOC/compute 不使用此 CB 策略。策略支持最多 64 个 Object 阶段读取，缺失属性或无法确定真假的策略拒绝挂载。配置在初始化时读取，当前不提供热切换。

使用 `multidevice/run.py --property-gate --expected-device 1` 验证设备 1 策略；其余必填参数可用 `--help` 查看。设备 0 策略与设备 1 策略应产生相反的设备选择，而两设备原始计算均继续通过。

## 报告与限制

[报告索引](../reports/README.md) 提供已保存的证据。对象树、属性和双设备隔离已验证；并发独立 launch、跨设备数据传输、可变状态属性和完整硬件枚举不在当前支持范围。

## 观测点与验证报告的详细参考

[39 点完整目录](tt-emule-hooks.md) 列出每个 hook 的参数宽度和上游位置；[报告阅读指南](tt-emule-reports.md) 逐项解释对象树、dispatch、records、filtered、属性检查和场景覆盖。先读这两页，可以避免把“API 被命中”当作“所有参数和硬件行为都被验证”。

## 准备工作的检查表

| 输入 | 锁定 revision / 要求 |
| --- | --- |
| tt-metal | `d48d09dee19de51f694a52fdf75d569950d38ceb` |
| tt-emule | `07f1bd8301544403c8bc1faa4038f6cbf69909f1` |
| uBPF | `8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521`，另需 scratch patch |
| 宿主 build | CMAKE_HOME_DIRECTORY 对应传入 metal；TT_EMULE_PATH 对应 emule；启用 emule |
| 编译器 | 本次源码构建的 fingolfin 可执行文件 |
| 输出路径 | 新目录，避免混入旧 package/adapter/结果 |

补丁按以下依赖准备：observability 探针和兼容修正 → NOC/compute/layout/multicast 场景 → lifecycle launch/repeated/exception → multidevice 场景。具体 apply 命令位于各专项 README。新 prepare 已处理 tilize 计数修正；旧安装需核对修正是否存在。已有探针不应重复运行带 `--apply` 的准备步骤。

`verify.py` 从已经配置好的外部环境开始；它会核验和重建，但不负责从零下载 tt-metal 依赖或代替宿主 CMake 初始化。仓库里的历史 `/tmp` 路径仅说明上次验证输入，不是用户机器上必然存在的安装路径。

## 完成一轮验证后保留什么

保留完整输出目录，特别是 stages、inputs、catalog、provenance、外部 working-tree patch 和新生成的探针头。仅保存 HTML 会丢失复现所需的命令和输入；仅保存 git revision 会丢失工作树补丁。归档时同时记录 Fingolfin 提交与编译器哈希。

如果只改文档，可运行链接检查和文档示例；改模型类型、probe ABI、provider 或生成 adapter 时，应重新构建相关 package/adapter，并执行对应真实场景。旧报告不会自动证明新源码有效。

## 设备 1 策略：从源码到验收

策略源码的关键部分为：

```fg
use tt_emule.types.{Observation, Core};
fn observe on tt_emule.device.core.cb.cb_push_back(
    call: Observation, core: Core, cb_id: u32, n: u32
)
where tt_emule: { device: { id: 1 } }
{}
```

使用本页前面的 build 命令生成 `/tmp/fg-device1-new` 后，完整验收命令为：

```sh
python3 integration/tt-emule/multidevice/run.py \
  --artifacts /tmp/fg-device1-new --metal /path/to/tt-metal \
  --build /path/to/tt-metal-build --output /tmp/fg-device1-results-new \
  --property-gate --expected-device 1
```

运行器设置门控环境并分别执行首次/缓存路径。expected-device 是测试 oracle，不参与生成谓词；因此把它改成 1 不能替代编译 device 1 策略。真正的条件来自 policy 文件、生成的 collect/predicate 和 C provider 返回的身份事实。

默认观测主体可能仍包含 `core.device == 0` 的参数条件，其 matched_context 不会随门控策略自动改成 1。gate 控制“是否进入程序”，body 条件控制“进入后做什么”，必须分别读懂。
