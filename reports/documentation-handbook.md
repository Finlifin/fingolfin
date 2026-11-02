# 手册扩充与示例验证

本轮针对文档缺少操作细节的问题补充构建矩阵、C 端到端教程、39 点参考、报告读法与排错；扩写语言、容器、util、架构和接入生命周期。新增 `examples/c-walkthrough` 为可执行文档，不修改运行时行为。

验证环境沿用首次基线中的锁定 uBPF scratch 源码与静态库，但 fixture、教程 C 库和示例重新生成/编译：

- `integration/c-runtime/prepare.py --compiler target/debug/fingolfin --output /tmp/fg-doc-fixtures-1` 成功。
- `examples/c-walkthrough` 以 Release、package/UBPF/scratch 开启、BUILD_TESTING 关闭配置，构建于 `/tmp/fg-doc-tutorial-1`；使用 `/tmp/fg-ubpf-scratch-1` 和 `/tmp/fg-final-emule-2/ubpf-build`。
- 执行教程打印 `factory_calls=2 counter[0]=28`、`record tag=7 value=11`、`record tag=7 value=17`、`detached, drained, destroyed`，内部检查 dispatch、FIFO 顺序和卸载成功。
- 手册新增的四个完整 `.fg` 代码块通过 check；容器章节 C vector 代码块使用 C11、Wall/Wextra/Werror 编译并运行通过。
- aggregate 参考执行确认 map key 7 的值为 2，histogram counts 为 `[2,0,0]`，total 为 2。
- 源码统一格式检查、本地文档链接检查和 `git diff --check` 通过。

本轮没有重跑完整 tt-emule 18 阶段，也没有把历史数字重记为新结果；引用的基线仍见 [首次验证](final-validation.md)。文档链接检查只检查本地文件目标，不验证外部网站与标题锚点。教程依赖参数化的本地 uBPF 路径，外部源码不随仓库分发。

## 原理与方法章节补充

新增工作原理、对象模型、ABI 心智模型、建模指导、观测指导五章，串联身份、属性、调用参数、两类帧、门控、状态与结果解释。修正 schema 参考中“C 包解析和生成 adapter 尚未接入”的过时描述。

本轮验证：观测指导的完整程序通过模型检查；现成 device_watch 入口成功生成 `.fgo` 和 C adapter。另用编译器生成的 record adapter 编译 C11 静态断言，确认文档中的 native header 56 字节、native frame 72 字节、BPF frame 32 字节以及参数偏移 56/64 和 16/24。该布局只适用于文档给定签名，不是通用入口布局。正式文档本地链接和 diff 空白检查通过。未修改运行时代码，未重跑外部模拟器。
