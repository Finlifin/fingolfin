# 文档目录

新用户按“快速上手 → 架构 → 对应接入指南”阅读。命令默认从仓库根目录运行；`BUILD`、`/path/to/...` 表示需要替换的本机路径。

## 使用

- [快速上手](quickstart.md)：编译器、参考执行、`.fgo` 和 C 基础库。
- [tt-emule](tt-emule.md)：39 个观测点、源码准备、完整验证、可视化和策略选择。
- [控制程序](../cli/README.md)：离线 inspect 与持续 Agent 会话。
- [能力范围](development-status.md)：当前支持范围与已知边界。

## 语言与模型

- [语言入口](language/README.md)：程序示例、内建 helper 与编译命令。
- [模块与类型](language/modules.md)、[模式语义](language/patterns.md)。
- [属性可用阶段](model-availability.md)：精确路径、Object 快照与失效责任。
- [语法来源](language/provenance.md)。

## C 接入与参考

- [架构](implementation.md)、[C 接入指南](c-integration.md)。
- [SDK](../sdk/README.md)、[路由与 lease](c-routing.md)。
- [对象注册表](runtime-objects.md)、[有界容器与状态](runtime-containers.md)、[util](utilities.md)。
- [调用帧 schema](frame-schema.md)、[Agent/Mount](../agent/README.md)、[stdio 协议](../agent/protocol.md)。
- [Rust VM 参考桥接](vm-bridge.md)：用于开发和对照，不是用户态产品实现。

## 维护与设计

- [开发指南](development.md)：格式化、测试、提交内容。
- [C 用户态架构决策](decisions/c-userspace.md)。
- [原始项目计划](../Fingolfin_Project_Plan_v0.1.md)：设计背景与长期范围，不是当前操作手册。
- [测试报告索引](../reports/README.md)：历史证据与当前验证入口。

各组件 README 只说明本组件的构建和契约。报告记录某次验证的输入与结果，不随源码格式化重写历史哈希。
