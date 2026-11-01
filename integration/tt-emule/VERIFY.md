# 一条命令重建并验证 tt-emule 集成

`verify.py` 面向已准备好的锁定源码和宿主构建配置，离线重建 userspace 依赖与 adapter，执行诊断、真实模型和故障恢复验证。它不使用旧 `/tmp` 目录中的 runtime/adapter 二进制，也不从历史报告读取 catalog；Rust 编译器可执行文件是显式输入，会记录其哈希。

```sh
python3 integration/tt-emule/verify.py \
  --compiler target/debug/fingolfin \
  --metal /path/to/tt-metal --emule /path/to/tt-emule \
  --metal-build /path/to/tt-metal-build \
  --ubpf-source /path/to/ubpf-with-scratch \
  --output /path/to/new-verification --jobs 2
```

输入要求：

- tt-metal、tt-emule、uBPF HEAD 与项目锁定 revision 一致。uBPF 已应用 `integration/ubpf/patches/safe-scratch-v1.patch`。
- tt-emule 已安装 observability 探针（39 个 API、51 处实现）、exp/sqrt 兼容修正、tilize num_tiles 修正。新 prepare.py 已包含后者，旧探针使用 tilize-count.patch。运行期 API 元数据由 `observability/catalog.py` 读取当前实际探针生成，缺少/无法识别实现即拒绝。
- tt-metal 已按 observability README 应用 noc、compute、layout、multicast 场景补丁，随后应用 lifecycle 的 launch、repeated-scenario、exception-scenario 补丁及 multidevice/scenario.patch。launch 与探针头使用支持 device v2 的最新版，详见 multidevice/README.md。
- 宿主 CMake 构建目录已配置。工具核对 CMAKE_HOME_DIRECTORY、TT_EMULE_PATH 和 TT_METAL_USE_EMULE，拒绝与传入源码不一致的构建目录。它构建 unit_tests_api，但不替代 tt-metal 的初始依赖配置。
- 本机已有 C/C++、BPF 工具链、CMake、Python、json-c/OpenSSL 依赖，以及编译好的 Fingolfin Rust 编译器。工具不执行网络下载或安装依赖。

流程：生成当前 catalog → 在新目录构建 PIC uBPF → 构建 PIC C runtime/agent → 编译 CB/compute/NOC 的 39 个具名 `.fg` 程序及 adapter → 链接动态对象运行时 → 构建宿主测试 → 六场景独立诊断 → 八场景动态身份/Mount 验证 → 生成的 `.fg` 属性谓词与 C provider 验证 → 四组故障恢复 → 双设备首次/缓存执行、正式 Mount 属性筛选与设备边界验证 → HTML 报告。任何阶段失败立即停止，保留该阶段日志与已完成证据；不自动删除或复用输出目录。

输出：

- `results/index.html`：真实场景、可展开对象树、属性读取及故障恢复对照表。
- `stages.json` 与阶段日志：命令、退出码、耗时、失败位置。
- `inputs.json`：输入路径、revision、编译器哈希；`*-working-tree.patch`：外部源码已应用的修改；`emule-fg_trace.h`：git diff 不包含的新探针头。
- `catalog.json`：当前插桩后源码的签名、槽映射、行号与哈希。
- `reference/`：本次独立诊断结果；`results/`：本次正式执行及恢复结果。
- `ubpf-build/`、`runtime-build/`、三个分组目录及 `dynamic/`：本次重建产物；`provenance.json`：源码与核心结果哈希。

这是可重跑的构建/验证流程，不承诺不同路径或工具链下的二进制哈希完全相同。当前验证边界包含普通、单设备 deferred 及双设备 deferred 身份隔离；跨设备 NOC/fabric、并发 launch、完整参数/重载、完整状态属性 provider、未覆盖的失败恢复与性能验收尚未完成。
