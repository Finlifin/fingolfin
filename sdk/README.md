# C SDK

目标端推荐使用 [fingolfin_v2.h](include/fingolfin_v2.h) 的 lease 接口。公共类型和帧头定义在 [fingolfin.h](include/fingolfin.h)。SDK 适用于 Linux x86-64 小端 ABI；C++ cmodel 可以直接包含这些头文件。

## 观测流程

```text
acquire → gate 固定订阅选择 → 按需构造 frame → dispatch → release
```

一次 lease 固定对象、配置代际和订阅资源。至少一个订阅允许时才调用 frame factory；全部拒绝时不构帧。`fg_v2_observe` 封装这一过程并返回分发统计，模型代码仍需处理失败。

ObjectKey 使用 run/launch/slot/generation，不把宿主指针当持久身份。CallHeader 包含 ABI 版本、frame 字节数、hook ID、配置代际和对象身份。native frame 与 BPF frame 是不同布局，由生成 encoder 显式转换。

## 生命周期

运行时函数表、context、adapter 代码与目标对象必须活到所有在途调用结束。detach 只停止新 lease，不取消已获取 lease；先排空，再销毁资源。还需由宿主排除销毁后新的 SDK 表调用，包括会返回 disabled/错误的调用。

完整发布、旧代际保活、关闭和并发契约见 [C 路由文档](../docs/c-routing.md)。组合 Program/Attachment/Host 的目标端入口见 [Mount](../agent/README.md)。

## 生成原生声明与 adapter

```sh
cargo run --locked -- abi --model models/tt_emule --output /tmp/tt-emule.h
cargo run --locked -- emit-adapter examples/tt_emule/device_watch.fg --model models/tt_emule --function program::watch --output /tmp/device-watch.adapter.h
```

`abi --rust` 另可生成 Rust repr(C) 参考绑定。规范符号编码为 `fg_…`，命令输出名称映射。struct 字段保持声明顺序，数组按值参数/返回值需用 struct 包装；bool 使用 uint8_t，enum 使用明确整数表示。生成文件包含大小、对齐和字段偏移断言，宿主仍需验证实际值与精确 hook 布局。

## 兼容接口与测试

v1 的 enabled/dispatch 宏保留用于早期 ABI 测试；两次独立调用不自动获得 v2 的资源保活保证。新接入应使用 v2。

`cargo test --test sdk_abi --test abi_codegen` 验证 Rust/C/C++ 布局和按值调用；C route、Mount 和真实 tt-emule 验证覆盖 v2 的实际执行链。测试范围与命令见 [开发指南](../docs/development.md)。
