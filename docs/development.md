# 开发与维护

## 工具

编译器使用 Rust 2024，C 库使用 C11/CMake 3.20+，C++ ABI 测试使用 Clang++。完整 Rust 后端测试需要支持 bpfel 的 Clang 和 llvm-nm。依赖由 Cargo.lock 固定。

格式化使用 rustfmt、clang-format、Black 和 cmake-format。Python 工具可安装到独立环境：

```sh
python3 -m venv .venv
.venv/bin/pip install -r development/requirements.txt
export PATH="$PWD/.venv/bin:$PATH"
python3 development/format.py
python3 development/format.py --check
python3 development/check_docs.py
```

C/C++ 使用根目录 .clang-format；Python 使用 pyproject.toml；CMake 使用 .cmake-format.json。`.fg` 没有通用语法重排器，脚本只统一现有行的缩进、尾部空白和文件结尾，保留语言 token 与负面测试输入。历史 reports、外部源码补丁和生成证据不重排，避免破坏哈希与补丁上下文。

## 日常检查

```sh
cargo test --locked
cargo clippy --locked --all-targets -- -D warnings
cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build/runtime -j 2
ctest --test-dir build/runtime --output-on-failure
```

Release 测试仍保留 assert。Sanitizer 可另建 Debug 目录并传 `-DFG_SANITIZE=ON -DCMAKE_C_COMPILER=clang`；LeakSanitizer 需要可扫描线程的环境。测试失败应保留日志，不能通过关闭检查来宣称整套验证通过。

## 完整 C 执行链

启用 FG_WITH_PACKAGE、FG_WITH_UBPF 后，测试还需 FG_TEST_ELF_DIR、FG_TEST_PACKAGE；FG_TEST_ADAPTER_DIR 启用生成 adapter/Mount 测试。用下列脚本生成所需 fixture，参数见帮助：

```sh
python3 integration/c-runtime/prepare.py --help
python3 integration/c-runtime/package_cases.py --help
```

uBPF pin 和 scratch 补丁见 [uBPF 准备](../integration/ubpf/README.md)。tt-emule 的完整验证按 [使用指南](tt-emule.md)，基础 CTest 不代表外部模型已经通过。

## 文档约定

使用说明在 docs，组件构建与 API 契约在组件 README，单次证据在 reports。增加功能时更新能力范围与相应指南；不要继续向根 README 追加开发流水账。历史报告不改写成新结果，新验证使用新目录。

链接检查覆盖正式文档与组件指南的本地 Markdown 文件目标；不联网检查外部 URL，也不验证 Markdown 标题锚点。报告中的绝对 /tmp 路径用于复现输入定位，不意味着这些文件随仓库分发。

## Git 内容

提交源码、模型、配置、文档、必要测试夹具与保存的报告。target、build、虚拟环境、Python 缓存、JIT 缓存由 .gitignore 排除；外部 tt-metal/tt-emule/uBPF checkout 不复制进仓库。提交前检查暂存清单和 diff --check；不得将环境凭据写进示例或日志。
