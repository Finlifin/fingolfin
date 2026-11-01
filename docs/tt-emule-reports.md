# 读懂 tt-emule 验证结果

验证包含独立诊断和正式 C 执行两条路径。前者回答“真实 API 是否被调用、采集了什么”；后者回答“对象是否正确注册、路由是否隔离、`.fg` 是否执行并产生正确状态”。两条路径的 callback 不同时 preload，避免把重复回调误当成额外覆盖。

## 从哪个文件开始

| 文件 | 适合回答的问题 |
| --- | --- |
| `stages.json` | 哪一步失败、执行了什么命令、花了多久？ |
| `inputs.json` | 用的是什么 checkout、编译器和输入路径？ |
| `catalog.json` | 哪些实现实际安装探针、采集了哪些参数？ |
| `reference/index.html` | 六个真实场景命中了哪些 API、事件顺序如何？ |
| `results/index.html` | 正式路由、对象树、属性、退役与恢复是否一致？ |
| `provenance.json` | 本轮输入/结果的哈希是什么？ |
| 各场景日志和原始 JSON | 页面摘要背后的逐项数据是什么？ |

统一验证目录的具体输出由脚本记录；历史归档可能将文件移动到报告子目录。可直接阅读 [已保存的正式报告](../reports/final-validation-1/tt-emule/index.html) 和 [验证记录](../reports/final-validation.md)。这些是特定源码版本的证据，不是重新运行命令的替代物。

## 诊断图的阅读顺序

先看每个 gtest 是否实际运行、是否成功，而后查看 API 命中图。未命中和未插桩是不同问题：前者需要补场景，后者需要补探针。按类别/API 筛选时间线，可检查 CB reserve→push、wait→pop 以及 NOC barrier 附近的调用关系。

时间线只显示筛选后的前 2,000 条，原始 JSON 保存完整事件。采样缓冲固定 65,536 条，存在 dropped 时本轮不能作为完整覆盖证据。trace 使用 mutex 和时钟，会扰动执行时间，因此页面耗时适合定位异常慢的阶段，不用于证明低开销。

线程编号是诊断探针编号，不是 kernel 或 fiber 身份；跨线程先后不能仅凭屏幕排列推断业务因果。scope 退出可能来自异常展开，不能因 entry/exit 配对就认为操作成功。

## 对象树的阅读顺序

展开 launch 根，再展开 device、core 和 CB/NOC/compute 叶节点。相同 core 坐标在不同设备下必须有不同身份；同一设备、同一核心下多个 hook 可以共享对象键，但每个 `(ObjectKey, hook)` 使用独立 Mount。

对象按实际事件惰性注册，所以没有出现在树中的设备不一定不存在，只能说明没有注册到本次观测树。根 id 是 run 编号；launch 是 ObjectKey 的 epoch，不能把根 id 当作不断变化的 launch 序号。

`ancestors` 从近到远保存 core、device、root 的完整键。`tree_checks` 按退役绑定计数，`tree_invalidations` 检查每个绑定的祖先失效，共享祖先会重复计数。这两项都不是“独立硬件对象数”。

## 正式路由计数的区别

| 指标/现象 | 含义 | 不能推出什么 |
| --- | --- | --- |
| dispatch | 进入正式执行路径的次数 | 不等于 emit 记录条数 |
| records | 程序提交的输出数 | 不等于观测点数 |
| matched_context | 程序体中的参数条件命中 | 不代表提前门控已筛掉其他对象 |
| filtered | 属性策略在 acquire 阶段筛掉 | 不是目标 API 被禁用 |
| skipped_events | 批次失败后停止观测的事件 | 不是正常策略过滤 |
| stale rejection | 退役路由/旧键被拒绝，factory 未执行 | 不代表所有并发 teardown 情况已覆盖 |
| property_checks | 活跃对象身份属性检查 | 不代表支持读取可变硬件状态 |
| stale_properties | 失效后属性成为 unknown | 不代表数值变为零 |

历史完整基线的六场景独立 trace 为 8,892 个事件、39 个 API；八场景正式链为 15 个 launch、9,084 次 dispatch、17,256 条记录和 294 个绑定。这些数字是当前场景与程序的回归基线，修改观测主体或场景后应重新解释，不能硬凑到旧数字。

## 场景究竟检查什么

| 场景 | 目标侧检查 | 观测侧价值 |
| --- | --- | --- |
| CB wraparound | 环形缓冲相关测试断言 | reserve/push/wait/pop 与指针获取 |
| DRAM copy | reader→compute→writer 数据路径 | 一个场景包含六次 launch，检查批次隔离 |
| NOC/信号量 | 四组搬运、inline 值 7、信号量值 1 | 单包调用、屏障、等待 |
| 算术/激活 | 十种运算各 1024 个 BF16 输出 | 算术与 unary hooks；exp 使用量化容差 |
| 布局转换 | 非均匀 32×64 输入，独立 tile 布局及还原 | 防止 tilize/untilize 错误相互抵消 |
| multicast | 两个接收核各检查 512 个字 | 三核事件、对象共享与隔离 |
| repeated | 同一 Program 两次执行 | launch generation 与独立状态 |
| exception recovery | 第一次主动抛异常、第二次正常执行 | 异常展开后的清理与恢复 |

算术输入并未覆盖所有负值/特殊浮点；双设备测试验证本地 CB，不代表跨设备 fabric 已通过。故障注入的 package 缺失、容量、宽度、BUSY 是合成控制器测试，应与真实场景分开理解。

## 属性策略的验收

双设备测试中两个设备仍应各通过自己的 512 字数据检查。开启默认 device 0 策略后，48 条事件中 24 次执行、24 次 filtered；反向 device 1 策略应交换设备归属。被筛掉的设备没有 frame factory、输出和计数增长，但原始计算继续完成。

首次和缓存运行都要看；缓存运行还要求 JIT DSO 内容和修改时间不变。若修改探针 ABI 却复用了旧 DSO，会产生错误身份或参数；当前补丁将探针头内容纳入缓存键，但并不追踪所有上游间接头文件。

## 发现异常后的排查顺序

先定位失败阶段，再确认输入 revision/补丁和 catalog，最后看场景原始日志。若 gtest 未实际运行，先修宿主测试选择；若真实数据错，先修场景或模型行为；若数据对而 dispatch/records 错，再检查身份、adapter、helper 配置和预算。失败目录保留，不要覆盖成“通过”目录。具体错误处理见 [排错手册](troubleshooting.md)。
