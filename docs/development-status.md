# 当前能力范围

本文描述当前交付基线；历史计划中的长期目标不自动计入本次已完成能力。测试证据按组件和执行范围分别解释，不能把基础库 sanitizer 结果扩大为整个仿真器的保证。

| 领域 | 当前支持 |
| --- | --- |
| 语言与编译 | 模块、固定宽度类型、函数、多入口、where 模式、类型 IR、C/BPF 生成、开发 `.fgo` |
| 模型契约 | 对象声明、hook 签名、精确属性阶段、内容哈希、原生布局与调用帧 schema |
| C 基础库 | util、有界 Vec/Map/Set/FIFO/Histogram、状态隔离、Host/helpers、对象树 |
| 执行 | 锁定 uBPF、安全调用帧、预分配 scratch/池、Program/Attachment/binding |
| 路由 | SDK v2 lease、不可变代际、门控、单 Port 多订阅、选择/解绑/排空 |
| 控制 | C Session、stdio Agent、fgctl inspect/session |
| tt-emule | 39/39 API 点、八个真实场景、对象树、双设备身份、异常恢复、身份属性、CB 属性筛选 |
| 可视化 | 场景结果、对象树、按设备执行／筛掉对照、诊断覆盖率与时间线 |

## 使用边界

- `.fgo` 为开发格式，不承诺跨版本稳定兼容；模型或帧契约改变后应重新生成包和 adapter。
- stdio Agent 管理程序注册，远端 attach、采集订阅、SSH/GDB 集成尚未交付。
- tt-emule 控制器每进程只支持一个活动 launch；双设备测试是同一 deferred launch 内的身份隔离，不是跨设备 NOC/fabric 传输验证。
- 对象按观测惰性建立；未提供完整硬件枚举、kernel/fiber 稳定对象、精确底层构造销毁和所有 API 参数/重载。
- 身份属性可在 Object 阶段形成快照；可变状态、计算/有状态属性、完整更新通知和通用 unknown 残余匹配仍待实现。
- CB 策略可由构建器选择 `.fg` 谓词；当前不是运行中任意用户策略热加载。
- 进程 abort、缺少 end、全部底层分配/VM 部分提交失败、整体性能与零分配验收不在当前集成证据覆盖内。

## 证据入口

使用 [报告索引](../reports/README.md) 查找当前验证和历史记录。最终收尾的测试命令与结果记录在 `reports/final-validation.md`。运行本机验证按 [开发指南](development.md) 和 [tt-emule 指南](tt-emule.md)。
