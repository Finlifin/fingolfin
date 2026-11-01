# C 单 Port 多订阅挂载

2026-11-01，`fg_mount_create_many` 支持一组 1–64 个同对象/hook 订阅的原子初始发布。复用现有路由分发与 gate，每项独立 Attachment、VM 和 Host，控制层负责准备全部资源、失败清理及整组 detach/drain。

真实生成 adapter/uBPF 测试覆盖：

- 64 个订阅持有同一旧租约，detach 后完成全部分发；每项初值 i，执行 5 后为 i+5，各自输出一条 5。
- 已知 false、helper 失败、正常三个订阅混合，factory 只调用一次，attempted=2、failed=1，正常订阅提交成功；全部 false 则 factory/attempted 均为零。
- 第二项入口不存在时，第一项已完成准备也会清理，整批输出为空。
- 0/65 项、hook/ObjectKey/native 大小不一致、索引越界拒绝；单入口既有生命周期测试继续通过。

GCC Release 通过，见 [release.log](c-mount-batch-1/release.log)。Clang ASan/UBSan/LeakSanitizer 下 mount、binding、gated_binding 三项通过，见 [sanitizer.log](c-mount-batch-1/sanitizer.log)。

这不是活动 Port 的动态订阅合并，也不是跨 Port/整包事务。可信模型 adapter 必须保证各项共享 native 帧布局；只检查大小无法证明布局一致。远端挂载、模型对象注册和跨入口共享状态仍待实现。接口契约见 [Agent 文档](../agent/README.md)。
