# C 开发包 loader 验证

日期：2026-11-01。范围：开发 v3 `development-function-frames` 包；不代表正式包协议或 Agent 已完成。

- GCC 16 独立 C 构建：7/7 CTest 通过。
- Clang 23、C runtime 和固定版本 uBPF 的 ASan/UBSan 构建：9/9 CTest 通过；包括原有 schema agreement 和 VM 检查。
- SHA-256 使用空串、abc、多块标准向量独立核对。
- 380 个离线包向量：合法编译产物、空白/键重排、转义键、Unicode/整数边界；截断、重复键、尾随内容、越界整数、浮点、非法 Unicode、版本/字段/hash/helper/schema/signature 错误。部分负例重新计算外层及 entry 哈希，覆盖更深的结构检查。
- 可选源码 hash pin、frame/decoded budget、输入释放后快照有效、查找/枚举边界通过。
- 开启 uBPF 时，纯 C 测试直接读取 `record.fgo`、使用包内 schema 初始化 frame、加载 ELF 并执行，C Host counter 和输出均为 37。运行测试无需 Rust runtime 或 Python 提取 ELF。

保存日志：[GCC](c-package-1/gcc.log)、[ASan/UBSan](c-package-1/asan-ubsan.log)。系统 json-c/OpenSSL 动态库未重新编译插桩；不能将结果表述为其内部代码已完成 sanitizer 验证。

## 复现

先按 runtime README 构建固定 uBPF，并用离线 Rust 编译器生成 fixture：

```sh
python3 integration/c-runtime/prepare.py --compiler target/debug/fingolfin --output /tmp/fg-c-package-fixtures
python3 integration/c-runtime/package_cases.py --input /tmp/fg-c-package-fixtures/record.fgo --output /tmp/fg-package-corpus.bin
cmake -S runtime -B /tmp/fg-package-build -DFG_WITH_PACKAGE=ON \
  -DFG_TEST_PACKAGE=/tmp/fg-c-package-fixtures/record.fgo \
  -DFG_PACKAGE_CORPUS=/tmp/fg-package-corpus.bin
cmake --build /tmp/fg-package-build
ctest --test-dir /tmp/fg-package-build --output-on-failure
```

完整 C VM 测试另启用 `FG_WITH_UBPF`，指定 `FG_UBPF_ROOT`、`FG_UBPF_BUILD` 和 `FG_TEST_ELF_DIR`；sanitizer 构建还需 `FG_SANITIZE=ON`，且 uBPF 使用相同 sanitizer flags。运行限制阻止 LeakSanitizer 扫描线程时，需在允许线程扫描的环境运行测试。

限制：无发布者认证；未验证 typed IR 完整语义、命名类型与模型一致性、指针目标模型、状态/输出 schema 或 simulator 身份。输入/token/深度上限限制 DOM 规模，decoded budget 仅限制提取的 ELF/schema 字节，不能当作进程总内存预算。
