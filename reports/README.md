# 测试报告索引

此目录保存独立验证的原始证据。报告名中的序号表示一次运行，不表示产品版本；历史日志、生成文件和哈希保持原样，不能用旧报告推断当前源码已验证。

## 当前使用入口

- [收尾验证](final-validation.md)：文档重整、统一格式化后的检查与命令。
- [可视化属性筛选报告](final-validation-1/tt-emule/index.html)：格式化后重建的对象树、39 点场景、设备执行／筛掉对照。
- [正式 Mount 属性筛选](tt-emule-property-gate.md)：编译器契约与 C 运行时链路。
- [对象树](tt-emule-object-tree.md)、[身份属性](tt-emule-properties.md)、[双设备](tt-emule-multidevice.md)。

## 分层证据

| 范围 | 报告 |
| --- | --- |
| C util / 容器 / 对象 | [基础库](c-containers.md)、[对象注册](runtime-objects.md) |
| Host / VM / 包 | [Host 与 VM](c-host-vm.md)、[scratch](c-scratch.md)、[Program](c-program.md)、[包](c-package.md) |
| SDK / 路由 / Mount | [路由](c-route.md)、[单入口](c-mount.md)、[多订阅](c-mount-batch.md)、[选择订阅](c-mount-select.md) |
| 属性与 gate | [事实采集](fact-collection.md)、[生成谓词](gate-codegen.md)、[gated binding](gated-binding.md) |
| Agent / CLI | [会话](c-session.md)、[stdio](c-agent-stdio.md)、[CLI 会话](c-cli-session.md) |
| 编译器 | [声明](declaration-checking.md)、[函数体](body-checking.md)、[ABI](abi-codegen.md)、[C 生成](native-codegen.md)、[BPF](bpf-codegen.md) |

其他文件是逐步开发时留下的历史证据。它们可能准确记录当时尚未实现的功能；当前用法与能力边界请以 [正式文档](../docs/README.md) 为准。

- [手册扩充与示例验证](documentation-handbook.md)：新增 C 教程、文档代码块和链接/格式校验；沿用历史模型验证证据。
