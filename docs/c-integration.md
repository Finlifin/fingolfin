# C/C++ 目标接入

C++ cmodel 可调用 C SDK；运行时、控制器和 Host 仍使用 C11。以下步骤描述目标进程内接入，独立 stdio Agent 的 LOAD 不会自动完成这些步骤。

## 构建层次

| 配置 | 依赖与产物 |
| --- | --- |
| 默认 `runtime` | C11、pthread；基础 `libfingolfin.a` |
| `FG_WITH_PACKAGE=ON` | json-c、OpenSSL；包加载和 `fgctl` |
| `FG_WITH_UBPF=ON` | 锁定 uBPF checkout/build；VM 执行 |
| 两者同时启用 | Program/Attachment/binding、Agent/Mount 和 `fg-agent` |
| `FG_UBPF_SCRATCH=ON` | 已应用本仓库 safe-scratch patch 的 uBPF；预分配执行 scratch |

```sh
cmake -S runtime -B build/full \
  -DFG_WITH_PACKAGE=ON -DFG_WITH_UBPF=ON -DFG_UBPF_SCRATCH=ON \
  -DFG_UBPF_ROOT=/path/to/ubpf -DFG_UBPF_BUILD=/path/to/ubpf-build \
  -DBUILD_TESTING=OFF
cmake --build build/full -j 2
```

外部版本与准备方式见 [uBPF 集成](../integration/ubpf/README.md)。启用完整测试还需提供生成的 ELF/包/adapter，见 [开发指南](development.md)。

## 接入顺序

1. 定义模型包的对象路径、hook 签名和固定宽度类型；只有具备稳定性保证的属性才声明提前可用阶段。
2. 用 `fingolfin build` 生成 `.fgo`，用 `emit-adapter` 生成对应原生帧和 encoder。模型与程序源码身份必须一致。
3. 创建 C Session 并加载 Program。为实际目标对象分配完整 ObjectKey，不用宿主地址充当持久身份。
4. 提供可信 hook ID 映射、native frame 大小、encoder、Host 配置和预算，调用 `fg_mount_create` 或 `fg_mount_create_many`。
5. 在目标 API 处通过 SDK v2 获取 lease；frame factory 只在至少一个订阅允许时执行。不要绕过 lease 独立调用门控和 dispatch。
6. 排空输出并检查各层返回状态。结束时 detach、drain、destroy，再失效对象键。

## 属性与状态

provider 必须在整个 facts collection 期间持有一致模型快照，检查完整父链，未知值不得冒充 0。Mount 复制 facts 与 gate；更新需要新的路由配置和旧 lease 排空。helper 失败不回滚已完成操作，调用方应按明确定义的错误策略继续或停止。

## 参考

- [SDK 接口](../sdk/README.md)、[路由并发契约](c-routing.md)。
- [Session/Mount](../agent/README.md)、[对象身份](runtime-objects.md)。
- [调用帧 schema](frame-schema.md)、[属性阶段](model-availability.md)。
- [tt-emule 接入实例](tt-emule.md)。
