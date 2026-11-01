# 全新目录重建与验证

[打开本次运行报告](tt-emule-repro-build-1/index.html)。统一入口从新目录完成 14 个阶段：重建 PIC uBPF、C runtime/agent、39 个 `.fg` 程序与 adapter，生成当前源码 catalog，执行独立诊断、动态对象路由和失败恢复测试。

八个真实场景、15 次 launch 通过，9,084 次成功 dispatch、17,256 条匹配输出；四组合成故障恢复通过。没有复用旧 runtime/adapter 二进制或历史 catalog。Rust 编译器是显式预构建输入，宿主初始配置及补丁仍需事先准备。

新增宿主 CMake 缓存路径校验，负向测试确认错误的 tt-emule 路径在任何输出目录或构建任务创建前被拒绝。catalog 根据当前探针检查 39 个 hook/51 处实现，记录插桩后源码的实际位置与哈希。

[复现命令与输入要求](../integration/tt-emule/VERIFY.md) · [14 阶段结果](tt-emule-repro-build-1/stages.json) · [输入版本](tt-emule-repro-build-1/inputs.json) · [错误路径拒绝证据](tt-emule-repro-build-1/preflight-rejection.json)。外部修改补丁、探针头、源码哈希和阶段日志一并保存。

此验证仍不覆盖多设备 mesh、完整模型参数/provider、所有失败恢复和性能验收，不能作为项目全部完成的证明。
