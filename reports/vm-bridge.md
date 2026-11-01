# Rust Host / uBPF 直接桥接验证

日期：2026-11-01。

新增可选 Cargo `ubpf` feature、build.rs、sdk/vm/bridge.c 与 src/vm.rs。固定上游 revision，链接外部已构建静态库；默认 Cargo 构建无需 uBPF。采用 safe interpreter、固定四项 helper 注册元数据、独立指令上限及预分配对齐帧。每次 execute 显式传入实际 Rust Host，不保存跨调用 Host 引用，不使用 TLS/global 当前程序。

验证从现有 .fg 源构建新的 .fgo，解码后加载 Entry ELF，直接读取实际 Rust 容器确认状态变化。三个 feature 集成测试覆盖四项 helper、Host 交替隔离、输出满与错误后重用、未知槽、帧容量/长度、计数溢出、map 满、i64 极值/分桶、histogram 溢出后的部分成功、指令预算、ELF 与帧头完整性、helper 清单不符。

执行证据：

- vm-bridge-default-tests.log：默认全量 109 个测试通过。
- vm-bridge-tests.log：首次启用 ubpf 的全量 111 个测试通过（当时新增 2 个）。
- vm-bridge-focused-tests.log：最终新增 3 个 VM 测试通过，包括后补的 histogram 溢出测试。
- vm-bridge-clippy.log：启用 ubpf 的 all-targets Clippy warnings-as-errors 通过。
- cargo fmt --check 和文档本地链接检查通过。

指令预算负例会由 uBPF 输出 “Instruction limit exceeded”，对应测试按预期收到 Runtime 错误。它不是测试失败。

库级桥接已实现；Port 到 VM 的正式分发、自动帧编解码、CLI 挂载、SDK 生命周期和生成 .fg 的真实 cmodel 端到端仍待实现。未宣称整个 VM 调用链零分配，也未将 checkout HEAD 检查当作静态库供应链证明。使用接口见 ../docs/vm-bridge.md。
