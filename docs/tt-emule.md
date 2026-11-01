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
