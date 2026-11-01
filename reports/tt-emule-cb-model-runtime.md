# 六个 CB hook 正式绑定验证

[打开场景与 hook 对照表](tt-emule-cb-model-runtime-1/index.html)。六个真实场景通过，共 3000 次成功 dispatch、3000 条逐条匹配阶段/序号的输出，错误 0。六个具名 hook 都有真实命中证据，零调用组合明确保留。

另有 24 个合成边界组合通过，验证两种帧布局、pages 零/非零、u32 溢出风险、非法 phase 与核/CB 过滤。旧单 hook 构建和四组边界测试也已回归。

[复现说明](../integration/tt-emule/cb-model-runtime/README.md) · [原始结果](tt-emule-cb-model-runtime-1/summary.json)。四份场景补丁已重放验证，生成程序、adapter、package 和实现哈希已保存。

范围仍限定单进程 core(0,0)/CB0 fixture。NOC/compute 的 33 个正式绑定、完整重载语义、对象生命周期/provider 和通用动态订阅未完成。表格为静态 HTML，尚未进行浏览器截图验收。
