# 从 `.fg` 到 C 进程：完整接入教程

本教程在一个单线程 C 程序中完成编译、加载、挂载、两次观测、结果读取和卸载。无需 tt-emule。完成后应看到计数器为 28，输出两条记录 11 和 17。源代码随仓库提供：[main.c](../examples/c-walkthrough/main.c)、[CMakeLists.txt](../examples/c-walkthrough/CMakeLists.txt)。

## 1. 准备环境和目录

从仓库根目录运行。需要 Rust 工具链、支持 bpfel 的 Clang、C11 编译器、CMake 3.20+、pthread、pkg-config、json-c 和 OpenSSL 开发文件，以及锁定版本的 uBPF 源码和静态库。uBPF 的准备流程见 [构建指南](build.md)。以下两个路径需替换成实际绝对路径：

```sh
export FG_UBPF_SOURCE=/path/to/patched-ubpf
export FG_UBPF_BINARY=/path/to/ubpf-build
cargo build --locked
python3 integration/c-runtime/prepare.py \
  --compiler target/debug/fingolfin --output build/tutorial-fixtures
```

fixture 输出目录必须不存在；重复实验可改成新的目录名。脚本编译两个现成程序，本教程使用其中的 record。它生成 `.fgo`、ELF `.o`、BPF frame `.h`、二进制 `.schema` 和 C `.adapter.h`，命令记录在 `commands.log`。这些文件必须来自同一次源码/模型组合，不能随意混用旧 adapter 和新包。

## 2. 理解被加载的程序

[record.fg](../integration/state-helpers/record.fg) 的完整内容是：

```fg
fn record(slot:u32,n:u64) {
    builtin.counter_add(slot,n);
    builtin.emit(7,n);
}
```

它是普通函数，便于先理解执行链；教程控制器将它显式映射到本地 hook 7。7 是本例的可信约定，不是全局注册的 tt-emule hook 编号。真正的模型程序可声明 `fn ... on 对象路径.hook(...)`，模型负责签名检查，见 [语言手册](language/README.md)。

`counter_add` 更新已经存在的 counter；`emit` 写有界 FIFO。两者都不会自动创建存储。因此 C 端在 Mount 创建前配置 counter 0 初值 0、输出容量 4，并允许 helper ID 1 和 2。

## 3. 构建并运行 C 程序

```sh
cmake -S examples/c-walkthrough -B build/tutorial \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DFG_WITH_PACKAGE=ON -DFG_WITH_UBPF=ON -DFG_UBPF_SCRATCH=ON \
  -DFG_UBPF_ROOT="$FG_UBPF_SOURCE" -DFG_UBPF_BUILD="$FG_UBPF_BINARY" \
  -DFG_WALKTHROUGH_FIXTURES="$PWD/build/tutorial-fixtures"
cmake --build build/tutorial -j 2
build/tutorial/fg-walkthrough build/tutorial-fixtures/record.fgo
```

预期标准输出：

```text
factory_calls=2 counter[0]=28
record tag=7 value=11
record tag=7 value=17
detached, drained, destroyed
```

C 程序链接 C runtime、C Agent 和 uBPF，不链接 Rust。Rust 的工作已在离线生成 `.fgo` 和 adapter 时完成。教程为使每一步契约失败立即显现，使用 assert 并在 Release 中显式保留断言；正式接入应将这些断言改成记录诊断、返回错误和逐级释放资源的控制流。

## 4. Session 加载为什么不等于开始观测

`fg_session_create(42, 1, ...)` 创建容量为 1 的 Program 注册表。请求指定 session 42、sequence 1、expected_generation 1 和 LOAD；包内容在调用期间提供，加载完成后可释放输入 bytes。

LOAD 的 reply 给出 `program_id`。此时尚没有对象、Host 或活跃 hook。控制器继续填写 `FgMountOptions`，再调用 `fg_mount_create`，成功后才取得可用于目标调用点的 `FgRuntimeV2`。

Mount 自己保留 Program 引用。因此卸载 Session 注册项不会让已发布 Mount 自动消失。应用必须保存 Mount 所有权并安排独立的 detach/drain/destroy；不能将 UNLOAD 当作停止目标执行的命令。

## 5. 每个身份字段的含义

本例 Attachment identity 为 `{program_id, 2, 3, 4, 5, 6}`：

| 字段 | 本例 | 谁分配、何时变化 |
| --- | --- | --- |
| program | LOAD 返回值 | Session 注册表 |
| configuration | 2 | 控制器的配置身份；不等同于路由 generation |
| run | 3 | 模型运行域 |
| launch | 4 | 模型批次/执行代际 |
| slot | 5 | 本次对象槽 |
| generation | 6 | 对象槽复用代际 |

ObjectKey 是后四项 `{3,4,5,6}`。`FgCallHeader.generation` 则来自 lease，表示路由快照代际，不能填对象 generation 6。教程的 factory 直接复制 lease 的 hook、generation 和 object，避免混淆这两种代际。

真实模型应从 [对象注册表](runtime-objects.md) 分配并验证对象键。本例是固定单对象演示，不伪装成硬件生命周期跟踪。

## 6. frame factory 的职责

`fg_v2_observe` 封装 acquire、门控、factory、dispatch 和 release。只有至少一个订阅需要执行时才调用 factory。factory 将 slot 0 和本次 amount 写入 `FgNativeFrame.arg0/arg1`，返回 header 地址；存储由 `Input` 持有，持续到同步 observe 返回。

生成的 `fg_generated_encode` 将原生 C 布局转换成 BPF schema 布局。不要把 native struct 强转成 BPF frame，也不要手工猜测 padding、字段偏移或预算前缀。C++ 调用同一 C SDK 时也遵守这个边界。

一次调用可能匹配多个订阅。SDK 只构造一次公共 native frame，各订阅独立编码和执行。检查 `report.attempted` 和 `report.failed`，不能仅以 factory 被调用判断所有程序成功。

## 7. 读结果和停止

两次调用分别提交 11 和 17，因此 counter 为 28，FIFO 依次返回 `(7,11)`、`(7,17)`。空队列返回 `FG_STATUS_NOT_FOUND`，这通常是正常排空终点。

本例单线程，因此 detach 后已经没有未完成 observe，可以确认 drained、读完输出、destroy Mount，最后 destroy Session。多线程应用必须先停止新调用来源、detach，并等待已有 lease 释放；destroy 之前还需在应用层排除所有 SDK table 调用，包括禁用状态下的 acquire。仅看到租约计数为零，不足以证明再无新线程访问借出的 table。

## 8. 可以做的三个实验

1. 将 `amounts` 改为 `{3,9}` 并同步修改校验值，计数器应为 12，记录顺序不变。
2. 将输出容量改为 1，第二次 emit 会失败；此前 counter_add 仍可能已经提交。把断言改成打印错误后检查状态，观察“单个操作失败不修改该容器”和“入口整体不回滚”的区别。
3. 把 Host counter 的 slot 改成 99，程序仍访问 slot 0，执行应失败。容量配置不是语言变量声明，必须与 helper 参数相配。

后两个实验故意制造失败，不属于默认成功命令。继续阅读 [C 接入指南](c-integration.md) 的配置字段、并发和失败清理，以及 [排错手册](troubleshooting.md)。
