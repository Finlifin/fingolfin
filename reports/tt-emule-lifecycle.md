# 真实 launch 生命周期验证

[打开对象键变化表](tt-emule-lifecycle-1/index.html)。真实执行边界已调用纯 C 身份注册表，三个场景通过：

- 同一 Program 连续执行两次：begin/end 2/2、96 条事件、两个槽位的 generation 从 1 增到 2、8 次旧键拒绝，错误 0。
- 算术场景：区分 CB0/CB1/CB16 与 compute 对象。
- Multicast：区分 core(0,0)、(1,0)、(2,0) 的 CB/NOC 对象。

[复现补丁与范围](../integration/tt-emule/lifecycle/README.md) · [原始结果](tt-emule-lifecycle-1/summary.json) · [源码哈希](tt-emule-lifecycle-1/provenance.json)。静态表尚无浏览器截图验收。

当前仅覆盖非 deferred launch 的身份注册/失效；mesh、异常路径实测、父子关系/provider、真实构造销毁，以及与 39-hook Mount 动态路由的合并仍未完成。
