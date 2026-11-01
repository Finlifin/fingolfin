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
