# C 目标侧单入口挂载核心

2026-11-01，新增 `FgMount`，从 C Session 获取 Program，连接 Attachment、生成 adapter、可选 gate 与专属 SDK Port。挂载完成前不暴露 runtime 表；失败清理所有已创建资源。

实际 `.fgo`/生成 encoder/uBPF 测试验证：执行产生计数 11；另取租约后卸载 Program 并销毁 Session，再 detach mount；新 acquire 被拒绝，旧租约仍执行 17，最终计数 28、两条输出分别为 11 和 17。租约未释放时 destroy 返回 BUSY，释放后排空输出并销毁成功。另覆盖身份不匹配、缺失入口、helper policy 拒绝、非法 native frame 大小、空对象和重复 detach。

GCC Release 通过，见 [release.log](c-mount-1/release.log)。Clang ASan/UBSan/LeakSanitizer 下 mount、binding、session 三项通过，见 [sanitizer.log](c-mount-1/sanitizer.log)。初次测试复用了 SDK 一次性租约而失败；修正为每次执行单独取得租约后复验通过，产品 SDK 语义未修改。

这是目标侧 C 生命周期原语，尚未接入真实模型对象注册、远端挂载协议、共享 Port 多订阅合并或整包原子挂载；不代表完整 attach/detach 控制面已交付。所有权及调用前提见 [Agent 文档](../agent/README.md)。
