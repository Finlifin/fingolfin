# 对象身份注册表验证

新增纯 C 有界对象注册表，为 run/launch/slot/generation 分配和校验提供身份层。槽位复用增加 generation，结束 launch 后旧键失效，并发 launch 互不串用。

基础 C 测试 9/9 通过；Clang ASan/UBSan/LeakSanitizer 下同样 9/9 通过。新增测试包含两个线程合计 4000 次注册/查询/退役，以及重复注册竞争。沙箱内 LeakSanitizer 受 ptrace 限制，已在获准的沙箱外重跑通过。

[API 与生命周期契约](../docs/runtime-objects.md) · [普通测试](runtime-objects-1/unit.log) · [内存检查](runtime-objects-1/asan-ubsan-leak.log) · [源码哈希](runtime-objects-1/provenance.json)。

尚未接入 tt-emule 真实 launch/device 边界；目标内存、父子关系、属性 provider 和在途 lease 排空不由此注册表管理，不能据此宣称完整生命周期已完成。
