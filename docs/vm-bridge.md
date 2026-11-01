# Rust Host 与 uBPF 桥接

本文记录 Rust 语义参考桥接，供开发与差分测试。目标端用户态实现采用 C，接入方式见 [C 接入指南](c-integration.md)。

可选 Cargo feature `ubpf` 提供 `fingolfin::vm::Vm`。默认构建不依赖 uBPF。当前面向已验证的 Linux x86-64 little-endian 环境，使用固定上游 revision `8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521` 的静态库。

## 构建与验证

先准备固定版本 uBPF checkout 和构建产物，方法见 [uBPF 集成目录](../integration/ubpf)。然后在仓库根目录执行：

```sh
export FINGOLFIN_UBPF_ROOT=/path/to/pinned-ubpf
export FINGOLFIN_UBPF_BUILD=/path/to/ubpf-build
cargo test --locked --features ubpf --test vm
cargo clippy --locked --features ubpf --all-targets -- -D warnings
```

构建脚本核对 checkout HEAD，并使用 clang/ar 编译 C shim，链接 `lib/libubpf.a`。静态库必须由该 checkout 构建；HEAD 检查不能证明现有库的来源或工作树未修改，发布重建证据仍待完成。

## Rust 接口

`Vm::load(&Entry, instruction_limit, max_frame)` 检查 ELF/帧头/签名完整性、ELF 结构及 helper 清单，创建 safe interpreter，注册固定 IDs 1–4 的 scalar-return metadata，并加载入口。指令上限与帧容量必须大于零。加载时预分配对齐的调用帧存储。

`Vm::execute(&mut frame_bytes, &mut host)` 将开发版 frame 复制到对齐存储，执行后复制回调用方。调用方必须按该 Entry 的 frame_header 构造参数、steps、max_depth 和 padding；不同函数不能共用未经核对的帧布局。生成入口检查精确长度。

每次执行显式传入同一个 Rust `intrinsic::Host` 实例。C helper 从当次 cookie 获取回调上下文，直接调用 Rust `Host::invoke_words`；没有 TLS 或全局“当前程序”。可信上下文位于 VM 可见 frame 区域之外。Rust 独占借用防止同一 VM 并发执行，Vm 不实现 Send/Sync。

| 返回值 | 含义 |
|---|---|
| `Ok(0)` | 生成入口成功 |
| `Ok(nonzero)` | 生成入口错误码，例如 5 为帧长度错误 |
| `Err(FrameCapacity)` | 超出加载时声明的调用帧容量，未执行 |
| `Err(Runtime(status))` | VM 执行失败，例如独立指令预算耗尽 |
| `Err(Host(error))` | Rust Host 的槽、容量、参数或溢出错误 |
| `Err(CallbackPanicked)` | callback panic 被捕获，不跨 C ABI 展开；panic=abort 的构建仍直接终止进程 |

帧和此前成功的状态更新在错误时保留；不提供入口级回滚。捕获 helper 错误后，当次后续 helper 调用返回失败。Host 生命周期由调用方持有，不被 VM 保存。

## 当前范围

真实测试从 `.fg` 构建新 `.fgo`，执行包内 ELF，验证 counter、output、map 和 histogram 操作落到实际 Rust 容器，覆盖状态交替、错误后重用、容量/未知槽、带符号边界、部分成功和指令预算。

这完成了库级 Host↔VM 连接；CLI attach、仅凭包元数据恢复 frame 编解码、SDK/Port lease 接入和生成 `.fg` 的完整 cmodel 集成尚未完成。C shim 预分配自己的帧缓冲，但没有宣称 uBPF 及整个调用链零分配。既有独立 C runner 继续用作交叉验证。

## 类型化函数适配

`vm::Function::load(&checked_program, &entry, instruction_limit, max_frame)` 提供自动编解码。它重新生成并核对调用帧头，拒绝与已检查源码/模型 ABI 不一致的 Entry。随后用共享的 `frame::Layout` 创建有界缓冲，布局也用于生成 C/BPF 的 offsetof/sizeof/alignment 断言。

`Function::call(args, bindings, steps, max_depth, host)` 接受参考执行器使用的 `pattern::Value`，返回 `Returned { value, remaining }`。支持固定宽度整数、bool、enum、结构体（含数组）及不解引用的指针；参数数目、字段、长度、范围和枚举值在执行前校验。hook 的绑定按名称提供；当前不会替调用方执行 pattern 匹配或确认对象生命周期。

```rust,ignore
let mut function = fingolfin::vm::Function::load(
    &checked_program, &entry, 10_000, 4096,
)?;
let result = function.call(
    &[fingolfin::pattern::Value::Integer(41)],
    &std::collections::BTreeMap::new(),
    10_000, 64, &mut host,
);
```

`CallError` 区分 Arguments、Execution、Entry 和 Result 错误。参数失败不会执行 VM 或修改 Host；每次 prepare 清零旧结果和 padding。入口或 helper 错误仍保留此前成功的状态更新。返回 Value 的复合结果会分配内存，不能当作零分配热路径。

该 Rust 参考适配器仍使用原始已检查程序。开发包已升级 version 3 并增加 [机器可读 frame schema](frame-schema.md)，正式 C 运行时通过独立 schema 消费布局；调用帧 ABI 保持不变。
