# 真实 CB 与 C 运行时集成

执行链为 tt-emule CB → C Session/Mount → C SDK v2 lease → 生成的 C adapter → C Attachment → uBPF → C Host。Rust 编译器离线生成 `record.fgo` 和 `record.adapter.h`；目标回调使用 C。

前置条件：按上级集成说明准备锁定版本的 tt-metal/tt-emule，将 `../cb-probe/hooks.patch` 应用到 tt-emule，并准备应用 `integration/ubpf/patches/safe-scratch-v1.patch` 的 uBPF 源码。已有补丁时不要重复应用。需要 C 编译器、BPF Clang、json-c、OpenSSL 和已构建的 Rust 编译器。

在仓库根目录执行（路径按本机调整）：

```sh
cmake -S /tmp/fg-ubpf-scratch-1 -B /tmp/fg-ubpf-scratch-pic \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release \
  -DUBPF_ENABLE_TESTS=OFF -DUBPF_SKIP_EXTERNAL=ON -DUBPF_INSTALL_GIT_HOOKS=OFF
cmake --build /tmp/fg-ubpf-scratch-pic -j4
cmake -S runtime -B /tmp/fg-runtime-pic \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DFG_WITH_PACKAGE=ON -DFG_WITH_UBPF=ON \
  -DFG_UBPF_SCRATCH=ON -DFG_UBPF_ROOT=/tmp/fg-ubpf-scratch-1 \
  -DFG_UBPF_BUILD=/tmp/fg-ubpf-scratch-pic
cmake --build /tmp/fg-runtime-pic -j4
python3 integration/tt-emule/c-runtime/build.py \
  --compiler target/debug/fingolfin --runtime-build /tmp/fg-runtime-pic \
  --ubpf-build /tmp/fg-ubpf-scratch-pic --output /tmp/fg-cb-new
python3 integration/tt-emule/c-runtime/run.py \
  --metal /tmp/fingolfin-tt-metal --build /tmp/fingolfin-emule-build \
  --probe /tmp/fg-cb-new --output reports/real-c-runtime-cb-new
```

两个脚本要求新输出目录。构建保存命令、日志和产物哈希；运行保存产物哈希、gtest XML、日志和精确计数。两个场景各运行 on/off/miss/dual/toggle；输出记录总和必须与 Host 页数计数一致。`dispatches` 表示 SDK 尝试分发次数，不是 VM 指令计数。

此夹具使用普通 `.fg` 函数和显式事件映射，限定 core(0,0)/CB0，miss 选择 CB16；run/launch/generation 固定为 1。首次事件初始化和全局 mutex 仅服务于集成验证。尚未验证完整模型 provider、生命周期、NOC/等待、热更新或性能验收。

`dual` 每个 hook 挂载两个独立订阅，共用一次构帧。`toggle` 在第 2 个事件前停用两个 Port，第 4 个事件前恢复，wrap 场景验证 push=3/pop=1，route generation 从 1→2→3。Session 在创建 Mount 后立即销毁，验证目标独立保活。这是测试内的确定性策略切换，不是远端控制命令。
