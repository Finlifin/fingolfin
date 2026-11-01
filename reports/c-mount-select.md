# C 挂载动态选择验证

2026-11-01，新增 `fg_mount_select`、`fg_mount_collect`、generation 查询与 `fg_binding_clone`，支持同一活动 Port 上已配置订阅的原子启停。替换使用独立 binding scratch 和复制的 gate/facts，保留 Attachment 状态，旧租约保活旧路由。

测试覆盖旧/新租约分别执行两个/一个订阅、交叉代际帧拒绝、空路由不构帧、重新启用保留计数、八组替换快照全部持租约时拒绝第九次更新且路由不变、释放后回收并继续更新、detach 后拒绝更新。两个工作线程共 4000 次观测与控制线程 500 次发布并行，最终成功执行数、counter 和输出记录一致。

GCC Release 通过，见 [release.log](c-mount-select-1/release.log)。Clang ASan/UBSan/LeakSanitizer 下 mount、route、binding、gated_binding 四项通过，见 [sanitizer.log](c-mount-select-1/sanitizer.log)。独立 ThreadSanitizer mount 测试通过，见 [tsan.log](c-mount-select-1/tsan.log)。

当前仅启停预配置入口，不更新模型事实、不增加新入口、不替换程序，也不提供多 Port 事务或远端命令。生命周期与资源上限见 [Agent 文档](../agent/README.md)。
