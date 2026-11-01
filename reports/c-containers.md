# C 用户态 util / 容器 / 状态基础

日期：2026-11-01。遵循用户确认的边界：Rust 编译器和 .fg 保留，用户态运行时、控制程序及 SDK 使用 C。

本次新增独立 runtime/CMakeLists.txt（仅 LANGUAGES C）、公共 C 头文件与 util/containers/state 实现。没有依赖 Rust Host，也没有改变编译器行为。实现 checked size/对齐、allocator、预算、counter、有界 vector/map/set/ring FIFO、histogram 和带六项身份字段的状态库。

验证使用 Clang 23.1.4 Debug + ASan/UBSan 和 GCC 16.2.1 Release，均按 warnings-as-errors 构建。CTest 的纯 C 测试程序通过，Release 也启用断言。覆盖容量/对齐/溢出/状态隔离/全部构造分配点失败清理；20,000 轮 map/queue 操作与独立数组模型比对；10,000 轮容器操作未增加 allocator 调用数。

第一次沙箱内执行因 LeakSanitizer 无法扫描线程失败，不是容器断言失败。获准沙箱外执行后，最终含状态与差分用例的 ASan/UBSan/LeakSanitizer 测试通过。没有将关闭 leak 检查的中间运行当作完整证据。

原始记录：c-containers-1/asan-ctest.log、release-ctest.log。readelf 显示 Release 测试程序唯一 NEEDED 为 libc.so.6；符号记录为 dependencies.log、symbols.log。独立 CMake 仅编译/链接 C 源与本静态库，未调用 Cargo 或链接 Rust 产物。clang-format --dry-run --Werror 通过。

当前尚未完成 C Host/helper/VM、正式 loader、SDK v2 lease、路由、Agent/CLI 和真实 cmodel 端到端。容器处理固定布局字节值，不管理外部指针资源；并发由所有者负责。分配计数证明只限已测容器操作，不代表整个产品调用链。
