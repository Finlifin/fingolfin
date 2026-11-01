# .fg 生成 BPF 与 safe interpreter 差分

日期：2026-11-01。

实现 emit-bpf 命令、生成宿主/BPF 共用布局断言的调用帧，以及 entry(frame,length) 包装。函数体内联以适应源语言按值结构与参数数量。Clang23.1.4 使用 bpfel/v3/freestanding 编译，uBPF 固定 revision 8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521，以 safe interpreter 和 10000 指令限制执行。

实际遇到并修复：

1. signed division 不能由 BPF target 直接编译，改用无符号幅值除/模并恢复符号，避免 signed-min 取负溢出。
2. uint64 乘法 overflow builtin 编译产生 __multi3 未定义调用，uBPF 拒绝加载。改为幅值界限除法预检查与无符号乘法；volatile bound 阻止 LLVM 重新折叠为不支持的宽乘。默认测试对生成对象执行 llvm-nm -u，拒绝外部未定义符号。

验证：80 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。原生后端的 UBSan 和差分测试也随算术 lowering 修改重新运行通过。真实 VM 脚本执行 9 个函数、363 组检查：336 组与参考执行器比对，27 组预算/深度/短帧检查。包括 signed/unsigned 极值、零除、溢出、余数、helper/短路与分支。

证据：generated-bpf-1/commands.log、summary.json 和各函数 cases.json；保存生成 C 与帧头供审阅。二进制仍在 /tmp/fg-generated-bpf-run-5，可由脚本重建。默认测试日志为 bpf-codegen-tests.log 和 bpf-codegen-clippy.log。

未完成：正式 .fgo 格式、自动模式/门控、完整 VM ABI/安全契约、复杂结构/多值关系 VM 差分、真实 cmodel .fg→VM、生命周期与状态/输出接口。未声称 M2 或完整开发通过。
