# Compute 正式模型绑定验证

[打开逐 hook / 场景对照表](tt-emule-compute-model-runtime-1/index.html)。19 个 compute hook 在三个真实场景中全部命中，共 2094 次成功 dispatch、3860 条逐项核对的输出，错误 0。114 个合成边界组合通过，覆盖四种参数数量、不同参数槽、u32 边界、阶段和 core 筛选。

修正了 tilize 五参数重载的 num_tiles 采集；生成器会拒绝仍缺少该字段的旧 catalog。该五参数重载本身尚无独立真实场景覆盖，现有布局场景调用三参数重载。

已有六个 CB 正式绑定加本次 19 个 compute，累计 25/39。余下 14 个 NOC hook、完整 API 参数/重载语义及对象生命周期尚待完成。当前绑定使用 core0 进程 fixture 身份，不能充当通用多设备注册。

[复现步骤与限制](../integration/tt-emule/compute-model-runtime/README.md) · [原始结果](tt-emule-compute-model-runtime-1/summary.json)。程序、生成 adapter、字段映射、package 和构建日志已保存。静态 HTML 尚无浏览器截图验收。
