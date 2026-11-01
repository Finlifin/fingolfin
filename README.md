# Fingolfin

Fingolfin 为硬件 cmodel 提供可编程观测：用 `.fg` 描述观测逻辑，由 Rust 编译器生成 BPF 程序和 C adapter，在目标进程的 C 运行时执行。用户态运行时、Agent、控制程序与 SDK 均使用 C。

当前 tt-emule 集成覆盖 **39 个 API 观测点**：CB 6 个、NOC 14 个、compute 19 个。提供真实设备／核心对象树、身份属性、按对象挂载、属性筛选，以及可展开的测试报告。

## 从这里开始

| 目标 | 文档 |
| --- | --- |
| 编译并运行第一个例子 | [快速上手](docs/quickstart.md) |
| 理解组件和执行链 | [架构](docs/implementation.md) |
| 编写 `.fg` 和模型 | [语言与模型](docs/language/README.md) |
| 接入 C/C++ 目标程序 | [C 接入指南](docs/c-integration.md) |
| 运行 tt-emule 与查看测试 | [tt-emule 使用指南](docs/tt-emule.md) |
| 格式化、测试和维护 | [开发指南](docs/development.md) |
| 查看当前边界和证据 | [能力范围](docs/development-status.md) · [报告索引](reports/README.md) |

完整导航见 [文档目录](docs/README.md)。

## 最小构建

需要 Rust 2024 工具链；C 基础库需要 CMake 3.20+ 和 C11 编译器。BPF 生成另需支持 `bpfel` 的 Clang。

```sh
cargo build --locked
cargo run --locked -- check examples/tt_emule/device_watch.fg --model models/tt_emule
cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build/runtime -j 2
ctest --test-dir build/runtime --output-on-failure
```

## 仓库结构

| 目录 | 职责 |
| --- | --- |
| `src/` | Rust 编译器、语义参考实现与开发工具 |
| `runtime/` | C util、容器、对象、Host、VM、路由与包加载 |
| `agent/`、`cli/` | C Session/Mount、stdio Agent 和 `fgctl` |
| `sdk/` | C ABI 与 SDK v2 lease 接口 |
| `models/`、`examples/` | 模型声明和 `.fg` 示例 |
| `integration/`、`tests/` | 集成驱动与回归测试 |
| `docs/`、`reports/` | 使用与设计文档、独立测试证据 |

这是当前需求对应的开发基线。开发包尚非稳定发布格式；通用远端挂载、可变属性维护、并发 launch 等边界集中记录于能力范围文档，避免把历史计划当作已交付功能。
