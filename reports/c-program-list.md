# C Agent 程序列表验证

2026-11-01，增加 `program.list` 和 C `fg_session_next`，提供按程序 ID 排序的一致列表、源码哈希和入口元数据。枚举取得独立 Program 引用；对象卸载/会话关闭后既有引用仍有效。

真实进程测试覆盖空列表、连续加载、卸载首个程序再复用槽位（结果为 ID 2、3）、哈希/入口/helper 与原始 manifest 对照、查询不改变 generation、重复请求拒绝、关闭后查询拒绝。C 测试覆盖游标结束、UINT64_MAX、非法输出参数、关闭后枚举及枚举引用跨销毁保活。

GCC Release 通过，见 [release.log](c-program-list-1/release.log)。session、agent_stdio 两项 Clang ASan/UBSan/LeakSanitizer 通过，见 [sanitizer.log](c-program-list-1/sanitizer.log)。协议见 [Agent 协议](../agent/protocol.md)。本轮未实现 hook 挂载、CLI 远端调用或跨连接恢复。
