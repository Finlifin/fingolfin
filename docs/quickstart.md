# 快速上手

## 环境

使用 Linux x86-64 小端环境。编译器需要支持 Rust 2024 edition 的 Rust/Cargo；BPF 生成需要带 `bpfel` target 的 Clang。C 基础库需要 C11 编译器、CMake 3.20+ 和 pthread。包加载另需 json-c/OpenSSL；执行 BPF 另需锁定版本 uBPF，见 [C 接入指南](c-integration.md)。

## 检查与参考执行

```sh
cargo build --locked
cargo run --locked -- check examples/tt_emule/device_watch.fg --model models/tt_emule
cargo run --locked -- eval tests/fixtures/valid_observation.fg --model models/fixture --function program::increment --args '[{"Integer":41}]'
```

最后一个命令返回整数 42，并输出执行步数与源码身份。`eval` 是 Rust 参考执行器，用于语言调试；目标进程中的用户态执行由 C 运行时负责。

## 生成开发包

```sh
cargo run --locked -- build examples/tt_emule/device_watch.fg --model models/tt_emule --output /tmp/device-watch.fgo
cargo run --locked -- explain /tmp/device-watch.fgo --entry program::watch
cargo run --locked -- emit-adapter examples/tt_emule/device_watch.fg --model models/tt_emule --function program::watch --output /tmp/device-watch.adapter.h
```

`.fgo` 保存 ELF、调用帧 schema、入口与模型身份；adapter 负责 C 原生帧到 BPF 帧的编码。生成包不会自动连接或修改目标进程，实际挂载需要 Session、Mount 和可信模型 adapter。

## 构建 C 基础库

```sh
cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build/runtime -j 2
ctest --test-dir build/runtime --output-on-failure
```

该配置不需要 Rust 链接或 uBPF，验证容器、对象、Host、facts、gate、路由、ELF/schema 等基础能力。

## 查看状态聚合

```sh
cargo run --locked -- eval integration/aggregate-helpers/aggregate.fg --model models/fixture --function program::aggregate --args '[{"Integer":7},{"Integer":-1},{"Integer":2}]' --host-config integration/aggregate-helpers/config.json
```

结果包含预分配 map 和 histogram 的更新。helper 的失败不会回滚此前已经成功的操作，容量与溢出规则见 [容器与状态](runtime-containers.md)。

## 下一步

真实硬件模型验证按 [tt-emule 指南](tt-emule.md) 操作；接入其他 cmodel 按 [C 接入指南](c-integration.md) 操作。`examples/fixture/cb_watch.fg` 是语法示例，含未实现的 `emit_pages`，不能替代上述可编译示例。

## 按目标选择第一条路径

| 你想先完成什么 | 建议操作 | 完成标志 |
| --- | --- | --- |
| 理解语言 | 执行本页 increment 和 aggregate | result=42；map[7]=2、histogram=[2,0,0] |
| 在 C 进程运行 `.fg` | [完整 C 教程](c-walkthrough.md) | counter=28、两条记录、正常清理 |
| 使用容器/util | 默认 C 构建，再看 [容器指南](runtime-containers.md) | 基础 CTest 通过，无需模拟器 |
| 看真实模型效果 | [tt-emule 指南](tt-emule.md) | 新验证目录有阶段记录和正式报告 |
| 修改模型/观测程序 | [语言手册](language/README.md) 与 [C 接入](c-integration.md) | 新包和 adapter 同源，目标结果校验通过 |

## 每种文件拿来做什么

| 文件 | 谁生成/消费 | 是否足以独立执行 |
| --- | --- | --- |
| `.fg` | 人编写，Rust 编译器检查 | 否，需要模型与执行环境 |
| `Fingolfin.toml` | 模型声明清单 | 否，声明源码目录和契约 |
| `.fgo` | build 生成，C package loader 消费 | 还需 Mount、Host 与目标调用 |
| `.o` | BPF ELF | 还需合法 schema/frame/helper 环境 |
| `.adapter.h` | emit-adapter 生成，C 编译器编译 | 提供原生 frame 和 encoder，不自动插桩 |
| `.schema` / frame `.h` | 后端生成，运行时/测试消费 | 定义 BPF 布局，不等于宿主结构布局 |
| 报告 JSON/HTML | 验证器生成 | 某次运行证据，不是配置文件 |

`build` 只产生开发包，不会寻找、连接或修改一个正在运行的 cmodel。目标进程需要显式集成 SDK 与模型 adapter。独立 `fg-agent` 提供控制会话，也不会自动给任意进程安装探针。

## 确认自己真的完成了上手

能够指出程序的入口名、模型路径、使用的 helper slot、对象身份和输出消费者；能区分参考 eval 和 C 执行；能说出输出为空时先看哪一层；能在停止目标调用后完成 detach/drain/destroy。若这些步骤仍不明确，先跑完 C 教程再进入 tt-emule，避免同时排查多个外部工程。

环境问题查 [构建指南](build.md)，具体错误查 [排错手册](troubleshooting.md)。
