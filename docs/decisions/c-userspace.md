# 架构决策：C 用户态与 Rust 编译器

## 决策

用户态运行时、控制程序、Agent 与 SDK 使用 C；保留 Rust 编译器和 `.fg` 语言。C++ cmodel 通过 C ABI 接入。Python 用于测试和报告，不作为运行时依赖。

## 目录边界

`runtime/` 提供执行与基础库，`agent/` 提供 Session/Mount 与 stdio Agent，`cli/` 提供 fgctl，`sdk/` 提供 ABI。`src/` 中的 Rust Host、路由和 VM 桥接保留用于语义参考与开发差分，不作为 C 目标程序的必需链接依赖。

## 约束

模型和程序使用明确内容身份，原生指针不能成为持久对象 ID。状态按完整程序/配置/对象身份隔离。对象销毁先阻止新 lease，再排空旧调用。容量、预算和 helper 错误显式传播；不能用日志替代控制面错误。

## 当前实现

C Program/Attachment/binding、Session/Mount、SDK v2 和 tt-emule adapter 已形成实际执行链。此文件记录已采用的架构，不再充当迁移任务清单。能力和边界以 [当前范围](../development-status.md) 为准。
