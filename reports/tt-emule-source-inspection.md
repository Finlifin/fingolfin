# tt-emule 源码核对（M0 尚未通过）

2026-11-01：已在 `/tmp/fingolfin-tt-emule` checkout `07f1bd8301544403c8bc1faa4038f6cbf69909f1`，其 tt-metal-pin.txt 为 `d48d09dee19de51f694a52fdf75d569950d38ceb`。可迁移的 pin 存于 integration/tt-emule/upstream.lock.toml。

直接源码观察：

- 根 CMakeLists.txt 仅定义 header-only INTERFACE target；完成 CMake 配置并不构成仿真器可运行证明。真实 host/JIT 集成须由 tt-metal 构建。
- include/jit_hw/api/cb_api.h 的 cb_push_back 更新读写指针、shared occupied、生产者标记、compute pack offset，并可更新 DFB bridge。post hook 需明确相对这些更新的阶段。
- cb_wait_front 有容量错误终止、occupied 快速返回、self-produce 快速返回和 fiber wait 路径；post-wait 不能只放在 fiber wait 后。
- cb_pop_front 更新读指针、occupied，并可更新 DFB ack/slot。int32 overload 转发到 uint32 实现，不应重复插桩。
- GETTING_STARTED.md 要求 clang-20/libstdc++ 和 UMD/tracy submodule。当前主机有 clang 23.1.4、g++、Ninja，未找到 clang-20；尚未证明不同工具链兼容。

尚未构建或运行真实 tt-emule，也未修改上游源码。tt-metal 下载第一次 TLS 失败后重试，下载状态需通过实际工具 session/进程核对，不能仅凭此文档推断它仍在运行。

本轮后续核对：tt-metal 已检出上述 pin，Tracy 为 `117100515bb21d9a6b3a8f0eee50ecd91f961408`，UMD 为 `4ed96fb3cc16526400b8baaa70744bc3319ec194`；两棵上游工作树 `git status --short` 为空。两个 submodule pin 已写入锁文件。

首次 CMake 配置因沙箱 DNS 限制失败；获得联网权限后，同一 build 目录重新配置并已开始下载 CPM 依赖。当前构建路径为 `/tmp/fingolfin-emule-build`，采用 Clang/LLD 23.1.4、Release、libstdc++，开启 TT_METAL_USE_EMULE 和 TT_METAL_BUILD_TESTS，关闭 Python、TTNN tests、Tracy、distributed 和 install。这是首轮 C++ 验证配置，不是完整发布配置。

可复现配置入口：`integration/tt-emule/configure.py` 校验四个 pin 后运行同一组选项并保存日志。默认使用上游 clang-20，当前环境需显式 `--cc /usr/bin/clang --cxx /usr/bin/clang++`；脚本目前只验证过 `--help`，正在运行的配置是先前直接启动的 cmake。

续接提示：配置工具 session 为 `52769`。写此记录时最近一次 poll 返回了新的 CPM 下载日志，session 仍存活。下一轮必须先 poll 此 handle；若 handle 已不存在，再核对 CMake 进程、缓存和 build.ninja，不能仅凭文档重新启动配置。

后续状态（取代前面的运行状态）：session 52769 已以 exit 1 结束。失败是 Arch host 没有对应 SFPI 二进制而触发源码构建，SFPI GCC 源码 libcody 遇到 `char8_t`/S2C 编译错误。没有将该错误归因于 Fingolfin 插桩。

已下载同版本官方 Debian x86-64 txz，SHA-256 与 tt-metal/sfpi-version 完全一致。`riscv-tt-elf-g++ --version` 为 tenstorrent/sfpi:7.67.0[761]，15.1.0。为了在 Arch 上显式指定此真实工具链，加入 `portable-sfpi.patch`，保留下游版本及动态库检查；没有改写系统发行版或伪造编译器。

新的 CMake session 17299 配置及生成成功（exit 0）。后续 `cmake --build /tmp/fingolfin-emule-build --target unit_tests_api -j 2` 已启动，session **10685**；日志 `reports/tt-emule-build.log`。最近核对仍在编译，不能声称目标构建或回归通过。续接时先 poll 10685 并读取该日志，切勿依据旧配置 session 重启下载。

构建后续：session 10685 以 exit 1 结束，670/889 的 ttexalens_elf 编译失败，具体是 ELFIO 的 elf_types.hpp 缺少 stdint.h 导致 uint16_t/uint32_t/uint64_t 未声明。已对固定 ELFIO `8ae6cec5d60495822ecd57d736f66149da9b1830` 添加单个标准头 include，补丁保存为 `integration/tt-emule/elfio-stdint.patch`；独立 include 的 C++20 语法检查通过。

恢复同一构建目录后，活跃构建 session 为 **80990**，日志为 **reports/tt-emule-build-2.log**。原日志保留失败证据。最近日志到 85/219，已越过 ttexalens 失败点，正在编译 tt-metal HAL。下一轮先 poll 80990，勿再使用已终止的 10685。

最新终态：session 80990 构建成功（exit 0）。已为 JIT 在 `runtime/sfpi` 创建指向验证过的工具链的符号链接，因为运行时 JIT 仅搜索该路径或 /opt/tenstorrent。session 24418 的基线 7 个测试通过；CB probe session 90490 的六组场景通过；插桩后无 runtime 的 session 5024 回归亦通过。当前没有剩余构建进程需要等待。真实结果及限制见 real-cb-spike.md，原始证据在对应 reports 子目录。
