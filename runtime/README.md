# C 运行时

独立 C11 库，提供目标端基础工具、状态、对象身份、Host、路由，以及可选的包和 uBPF 执行链。它不链接 Rust Host；编译器事先生成 `.fgo` 与 adapter。

## 构建

```sh
cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build/runtime -j 2
ctest --test-dir build/runtime --output-on-failure
```

默认配置只需 C11、CMake 3.20+ 和 pthread，产物为 libfingolfin.a。启用 json-c/OpenSSL 包支持、uBPF 或完整测试的方法见 [C 接入指南](../docs/c-integration.md) 与 [开发指南](../docs/development.md)。

Sanitizer 构建使用 `-DCMAKE_C_COMPILER=clang -DFG_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`。LeakSanitizer 需能扫描线程的执行环境，ptrace/sandbox 限制导致的失败不能解释为代码通过或内存错误。

## API 导航

| 头文件 | 用途 |
| --- | --- |
| util.h / bytes.h / elf.h | 状态码、预算、allocator、checked 运算、字节和 ELF 校验 |
| containers.h / state.h | 有界容器和完整身份隔离的状态 |
| objects.h | launch、对象键、父子关系、槽复用代次 |
| host.h | 四项 helper、预分配状态和输出 |
| gate.h / facts.h | 阶段门控、三值谓词、属性快照 |
| route.h | Port、不可变配置、lease 与回收 |
| schema.h | 原生/BPF 帧契约与输入值验证 |
| package.h / program.h | 开发包加载、Program 保活 |
| attachment.h / binding.h | 执行实例、encoder、门控订阅 |
| vm.h / pool.h | uBPF 执行、scratch 与预分配实例池 |

API 的所有权和失败输出约定以各头文件注释为准，不假定所有函数使用同一种清空输出规则。

## 执行与错误

Host 的 counter/map/histogram/output 在创建时配置。helper 不会隐式创建未知 slot，失败不会回滚此前成功操作。VM 先验证 ELF/helper mask，再按精确 schema 编码和校验输入。`FG_STATUS_OK` 只说明对应层成功，还需检查 VM/entry/helper 状态。

同一 VM 不支持同时执行，资源占用返回 BUSY；对象和 Host 的同步由所有者负责。Mount 将 Program、Attachment、Host 与 Port 组合起来，避免调用者自行拼接生命周期；详见 [Agent/Mount](../agent/README.md)。

## 进一步阅读

[容器与状态](../docs/runtime-containers.md) · [对象身份](../docs/runtime-objects.md) · [路由](../docs/c-routing.md) · [帧 schema](../docs/frame-schema.md) · [util](../docs/utilities.md)。
