# C Host 与真实 uBPF 集成

日期：2026-11-01。

新增 host.h/host.c，配置阶段一次性创建所有 counter、map、histogram 和输出槽；四项 helper 使用实际 C 容器，预分配后无扩容。C Host 测试验证四项 helper 各 10,000 次调用无新增 allocator 调用，所有构造分配点失败均回收，另覆盖非法参数/未知槽/容量/溢出/隔离。

新增 vm.h/vm.c 与可选 FG_WITH_UBPF。执行数据由 C 拥有，aligned frame 外的可信 cookie 持有当次 Host；safe interpreter、scalar-return metadata、显式 helper mask、独立指令预算。禁止同一 VM 重入/并发占用的 atomic flag 已实现；Host 的跨 VM 同步仍由所有者负责，当前测试未宣称完成多线程压力验收。

从 .fg 生成的 record/aggregate ELF 已在纯 C 测试程序中执行。覆盖 C Host 隔离、输出满前的计数保留、计数/直方图溢出、map 容量、未知槽、短帧与超容量、非法 ELF、缺少授权 helper、预算耗尽及错误后重用。

首轮负例发现：先注册通用 dispatcher 会使 uBPF 的加载验证采用通用 validator，放宽缺 helper 的加载检查。已修复为只注册 allowlist metadata，先加载验证，再安装 dispatcher；执行 cookie 再检查 mask。负例已通过。运行期 uBPF error_printf 接入不打印的回调，错误由状态/result 返回。

验证：Clang 普通构建的三个 CTest 程序全部通过；C runtime 与 uBPF 库同时启用 ASan/UBSan 后，沙箱外包含 LeakSanitizer 的三个 CTest 程序全部通过。GCC Release 的默认 C 基础库与 Host 两个测试通过。格式检查通过。

原始证据见 c-host-vm-1/：两套 CTest 日志、链接命令、uBPF sanitizer flags、动态依赖/未定义符号、新生成的 .fgo 与帧头及编译日志。目标程序只链接 C runtime、uBPF 与系统库，不调用 Rust；Rust 仅在离线阶段生成测试输入。

限制：当前入口是开发 ELF loader，不包含正式 .fgo/schema/身份协议校验。已有 uBPF safe interpreter 在执行时仍分配 shadow/spill 元数据，本次只证明 Host helper 操作零分配，不宣称完整 VM 热路径达标。资源 scope、SDK lease、正式模型 adapter、Agent/CLI 与 cmodel 集成待完成。
