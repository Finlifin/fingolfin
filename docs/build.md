# 构建与依赖

所有命令从仓库根目录运行。源码支持层次化构建：可以只使用 C 容器/对象库，也可以启用开发包加载、VM 和完整 Session/Mount。不要为了验证容器先搭建 tt-metal。

## 依赖与产物矩阵

| 目标 | 必需依赖 | 产物/用途 |
| --- | --- | --- |
| Rust 编译器 | 支持 edition 2024 的 Rust/Cargo、Cargo.lock 依赖 | `target/debug/fingolfin`，解析、检查、生成 |
| BPF 后端 | 支持 bpfel 的 Clang，测试另需 llvm-nm | ELF 与 `.fgo` |
| C 基础库 | C11、CMake ≥3.20、pthread | `libfingolfin.a`，对象/容器/Host/gate/路由 |
| 包加载 | pkg-config、json-c ≥0.15、OpenSSL Crypto | package API 与 `fgctl` |
| VM | 固定版本 uBPF 源码和静态库 | VM/pool |
| 包加载 + VM | 上述两组依赖 | `libfingolfin_agent.a`、`fg-agent`、Session/Mount |
| tt-emule | C++ 工具链、锁定外部源码、宿主已配置构建目录 | 真实模型验证，见专项指南 |

当前验证环境为 Linux x86-64 小端。`.fgo` 是开发包格式，跨版本兼容承诺见 [frame/schema](frame-schema.md)，不能仅凭文件后缀认为任意版本可加载。

## 最小构建

```sh
cargo build --locked
cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build/runtime -j 2
ctest --test-dir build/runtime --output-on-failure
```

基础配置运行 9 个 C 测试。不同功能开关会增加测试数量；单看“全部通过”而不看构建配置，会误判覆盖范围。默认 C 构建不调用 Cargo，也不要求 uBPF。

## 准备 uBPF

输入 checkout 必须位于 revision `8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521`。使用你已经取得的锁定源码生成隔离的 patched checkout，避免在共享外部工作树上重复打补丁：

```sh
python3 integration/ubpf/prepare-scratch.py \
  --source /path/to/pinned-ubpf --output /path/to/new-patched-ubpf
cmake -S /path/to/new-patched-ubpf -B /path/to/new-ubpf-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DUBPF_ENABLE_TESTS=OFF -DUBPF_SKIP_EXTERNAL=ON -DUBPF_INSTALL_GIT_HOOKS=OFF
cmake --build /path/to/new-ubpf-build -j 2
```

产物必须包含 `lib/libubpf.a`。`FG_UBPF_ROOT` 指向源码根，`FG_UBPF_BUILD` 指向构建根，不是 `lib` 子目录。`FG_UBPF_SCRATCH=ON` 要求源码确实已应用 safe-scratch patch；HEAD 相同本身不能证明补丁已应用。PIC 对将静态库链接进 tt-emule observer `.so` 尤其必要。

## 完整 C 构建与测试

先生成 fixture，再配置 CMake。输出目录不存在时执行：

```sh
python3 integration/c-runtime/prepare.py \
  --compiler target/debug/fingolfin --output build/c-fixtures
cmake -S runtime -B build/full -DCMAKE_BUILD_TYPE=Release \
  -DFG_WITH_PACKAGE=ON -DFG_WITH_UBPF=ON -DFG_UBPF_SCRATCH=ON \
  -DFG_UBPF_ROOT=/path/to/new-patched-ubpf \
  -DFG_UBPF_BUILD=/path/to/new-ubpf-build \
  -DFG_TEST_ELF_DIR="$PWD/build/c-fixtures" \
  -DFG_TEST_PACKAGE="$PWD/build/c-fixtures/record.fgo" \
  -DFG_TEST_ADAPTER_DIR="$PWD/build/c-fixtures"
cmake --build build/full -j 2
ctest --test-dir build/full --output-on-failure
```

在已验证的 Linux scratch 配置中，这条完整链包含 24 个 CTest，覆盖 package、CLI、stdio Agent、Session、Mount、生成 adapter 和 VM。`FG_TEST_ADAPTER_DIR` 不能遗漏，否则少跑 Mount/生成 adapter 相关测试。

仅交付库或工具时可用 `-DBUILD_TESTING=OFF`，免去 fixture 前置条件；这不产生测试通过的证据。只加载包而不执行可单开 `FG_WITH_PACKAGE`，此时仍需设置测试 package，或关闭测试。

## 安装到自己的工程

仓库当前通过 CMake target 和静态库集成。参考 [C 教程的 CMake](../examples/c-walkthrough/CMakeLists.txt) 使用 `add_subdirectory` 并链接 `fingolfin_agent`；其传递依赖包含 runtime 和必要系统库。只使用基础能力则链接 `fingolfin`。部署时保存与包对应的模型身份、adapter 和 encoder 代码，encoder 必须活到最后一个 Mount 销毁。

## 可重复验证

`Cargo.lock` 固定 Rust 依赖；uBPF、tt-metal、tt-emule 使用 revision pin。编译器二进制、外部补丁、编译选项及输入模型也会影响结果。tt-emule 验证脚本记录这些输入，但不承诺不同路径/工具链产生逐字节相同的二进制。

格式化与日常回归见 [开发指南](development.md)；配置失败、缺失 bpfel、库布局错误见 [排错手册](troubleshooting.md)。
