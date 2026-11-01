# 生成程序 → C Host/VM

开发环境先构建 Rust 编译器，再离线准备两个测试程序。输出目录必须不存在：

```sh
cargo build --locked
python3 integration/c-runtime/prepare.py --compiler target/debug/fingolfin --output /tmp/fg-c-elf
```

产物包含 version 3 `.fgo`、ELF、机器可读 `.schema`、生成帧头和命令日志。旧 fixture 目录需要重建。可以把 ELF 目录交给没有 Rust 工具链的目标构建环境；后续 CMake/CTest 不调用 Cargo 或 Rust 编译器。

准备固定版本 uBPF（revision `8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521`）及静态库后：

```sh
cmake -S runtime -B /tmp/fg-c-vm \
  -DFG_WITH_UBPF=ON \
  -DFG_UBPF_ROOT=/path/to/pinned-ubpf \
  -DFG_UBPF_BUILD=/path/to/ubpf-build \
  -DFG_TEST_ELF_DIR=/tmp/fg-c-elf
cmake --build /tmp/fg-c-vm
ctest --test-dir /tmp/fg-c-vm --output-on-failure
```

`FG_WITH_UBPF` 默认 OFF；开启并启用测试时，缺少 ELF 会在配置阶段明确失败。uBPF checkout HEAD 核对不等于静态库来源证明，必须从对应 checkout 构建该库。

测试涵盖 generated counter/emit 和 map/histogram、Host 隔离、allowlist 加载拒绝、非法 ELF、帧容量/长度、溢出、缺槽、指令预算、失败后重用和跨 helper 部分成功。容器与 Host 测试也一起运行。需要完整 sanitizer 检查时，runtime 使用 `FG_SANITIZE=ON`，uBPF 静态库也应使用同一编译器及 `-fsanitize=address,undefined -fno-omit-frame-pointer` 构建。

这是开发 ELF/frame 级集成。没有声称正式包 loader、自动模型 adapter、SDK v2、并发路由和真实 cmodel 接入已完成。VM 测试中的帧针对这两个 fixture；正式运行时仍需包 schema 与生成 adapter。
