# 开发版 .fgo 构建与读取

日期：2026-11-01。

新增 build/explain CLI，默认构建全部 hook 入口，可用 --function 显式选择函数。Clang 使用固定 bpfel/v3/freestanding/O2 参数，暂存目录以独占创建方式分配并在结束清理，所有入口成功后才原子发布。支持 --model-hash 源码 pin。

容器 profile=development-function-frames、version=1，JSON 编码并设 32 MiB 读取限制。记录每入口 ELF、函数/钩子签名描述、直接依赖、宿主帧头、各项 SHA-256，以及整体 manifest SHA-256；含程序/模型源码身份、编译器/Clang 版本与参数、带 span 的类型 IR。相同输入/工具链在本机重复构建字节一致。

ELF 检查限定 ELF64 little-endian BPF relocatable，检查 section/string/symbol 范围、未定义符号和唯一 global entry。它不是 VM 指令验证器。完整性哈希不能证明作者、模型真实性或 ABI 兼容性。

验证：84 个 Cargo 集成测试、Clippy warnings-as-errors 和格式检查通过。新增测试覆盖双入口重建、编解码、包内容损坏、版本错误、32 MiB 限制、ELF 各字节截断与坏范围、构建失败保护和 explain 入口选择。

真实执行脚本改为构建 .fgo，经 explain 校验后执行包中 ELF：9 个函数共 363 组检查再次通过。原始证据在 packaged-bpf-1/（含 .fgo、输入/预期/实际结果、命令日志）；Cargo/Clippy 日志为 package-tests.log/package-clippy.log。

缺口：完整发布包仍需要 simulator build ID、独立 schema/ABI/hook/物理绑定 fingerprint、配置布局、门控计划、状态定义、输出 schema、helper 需求、正式加载和卸载协议。已有 signature_hash 是开发描述哈希，不能等同于冻结后的 hook ABI fingerprint。未声称项目计划第 463 行的完整包格式已交付。
