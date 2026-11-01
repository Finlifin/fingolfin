# 真实 CB：Session/Mount 与动态订阅

真实 tt-emule 集成已改接 C Session/Mount；两个 CB gtest × on/off/miss/dual/toggle 共十组通过，见 [精确计数](real-c-mount-cb-1/probe-summary.json)。dual 的 wrap 场景只构帧 5 次，分发/输出 10 次，push=8/pop=4。toggle 在第 2、3 个事件停用，第 4 个恢复，构帧/分发/输出各 3 次，push=3/pop=1，两 Port generation 均为 3。所有 errors=0。

Session 创建 Mount 后立即销毁，真实目标仍完成 `.fg` 执行，验证 Program 独立保活。输出总和与 Host 计数核对，并在结束时 detach/drain/destroy。每组 gtest 日志与 XML 位于同目录，产物哈希见 [artifacts](real-c-mount-cb-1/artifacts.json)。

此处仍是固定 core0/CB0 身份、显式函数映射、测试内确定性启停与全局 mutex；没有声称远端挂载、完整模型或性能验收。新观测地图另见 [39 点报告](tt-emule-observability.md)。
