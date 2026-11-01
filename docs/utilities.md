# 公共 util

编译器工具与目标端基础工具分别实现，避免 C 用户态链接 Rust。

## C 目标端

| 头文件 | 职责 |
| --- | --- |
| util.h | FgStatus、allocator、checked size/对齐、counter、预算 |
| bytes.h | 有界字节视图与读取 |
| elf.h | BPF ELF 前置结构校验 |
| schema.h | 机器可读调用帧布局、参数和值验证 |
| objects.h | 有界对象身份、父子关系与代次 |
| facts.h / gate.h | 复制事实、预算化读取、三值谓词与阶段门控 |

预算不足保持余额不变，调用方必须传播失败。包、ELF 和 schema 的结构校验不等同于程序来源认证；VM 指令校验仍独立执行。

## Rust 编译器

| 模块 | 职责 |
| --- | --- |
| util::budget | 显式工作额度 |
| util::artifact | 同目录临时文件与原子发布 |
| util::text | UTF-8 字节位置到行列映射 |
| util::hash | SHA-256 与长度前缀内容哈希 |
| util::elf | ELF 校验和实际 helper ID 提取 |
| util::containers | 有界容器参考实现 |

模型内容哈希用于精确源码身份，不代替 ABI 兼容性或身份认证。语法源位置按字节记录，不能把字符索引直接当字节偏移。

容器详见 [容器与状态](runtime-containers.md)，目标接入见 [C 接入](c-integration.md)。
