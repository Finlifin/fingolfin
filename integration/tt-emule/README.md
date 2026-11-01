# 固定版本的 tt-emule 集成环境

最新动态模型集成可通过 [统一重建与验证入口](VERIFY.md) 在新目录完成依赖构建、39 个 adapter 生成、真实场景和失败恢复验证。

源码和子模块 pin 在 `upstream.lock.toml`。构建入口是 tt-metal，不能把 tt-emule 自身 header-only CMake target 配置成功当成仿真器可运行。

当前环境：

- `/tmp/fingolfin-tt-metal` 与 `/tmp/fingolfin-tt-emule`：固定 checkout。
- `/tmp/fingolfin-emule-build`：Release C++ 测试构建，Python/TTNN tests/distributed 关闭。
- `/tmp/fingolfin-sfpi`：官方 SFPI 包解压后的父目录，包含 `sfpi/compiler/bin`。

Arch 没有匹配发行版的 SFPI 预编译包，自动源码构建在本机 GCC 16 失败。可选的 `portable-sfpi.patch` 给 upstream CMake 增加显式工具链父目录，仍执行真实版本和动态库检查。已使用锁文件中的官方 Debian archive，SHA-256 校验通过；这不代表所有 Debian binary 在 Arch 上均兼容，只记录此工具链已通过当前配置检查。

首次准备时，对锁定 tt-metal tree 应用补丁；重复运行前先检查是否已经应用：

```sh
git -C /tmp/fingolfin-tt-metal apply --check /home/fin/apps/fingolfin/integration/tt-emule/portable-sfpi.patch
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/portable-sfpi.patch
```

复现配置（先确认没有相同目录上的活跃配置/构建进程）：

```sh
python3 integration/tt-emule/configure.py \
  --metal /tmp/fingolfin-tt-metal --emule /tmp/fingolfin-tt-emule \
  --build /tmp/fingolfin-emule-build --cc /usr/bin/clang --cxx /usr/bin/clang++ \
  --sfpi-root /tmp/fingolfin-sfpi --log reports/tt-emule-configure.log
cmake --build /tmp/fingolfin-emule-build --target unit_tests_api -j 2
```

上游推荐 clang-20；这里显式选择当前 clang 23.1.4，尚需真实功能回归验证。首次配置会下载上游锁定的 CPM 依赖。源码路径为本机示例，迁移环境需自行选择路径。当前构建状态以报告和实际工具进程为准，尚无 CB/NOC/等待场景通过证据。

当前 Clang 23/GCC 16 系统头组合还需要 ELFIO 的显式 stdint.h include 补丁。CPM 首次配置取得 ELFIO 后，在对应 `elfio` checkout（其 HEAD 必须匹配锁文件）执行 `git apply --check` 和 `git apply`，补丁为 `elfio-stdint.patch`。本机路径为 `/tmp/fingolfin-tt-metal/.cpmcache/elfio/e273dd7e3097c96c296e1629de107793840f227d`。该改动只修正头文件自包含性，未改观测或仿真行为。

JIT 的运行时路径还需要真实 SFPI：如果 `tt-metal/runtime/sfpi` 不存在，可将其链接到已校验解压目录的 `sfpi` 子目录。不得覆盖既有工具链。当前 host/JIT 基线子集已通过，见 reports/real-cb-spike.md；这不代表完整发布回归。
