# C Agent 会话核心

`fingolfin_agent` 是独立 C11 控制库，公开接口见 [session.h](include/fingolfin/session.h)。启用 runtime 的 `FG_WITH_PACKAGE` 与 `FG_WITH_UBPF` 时构建，复用 C Program 和包校验器。`fg-agent` 可执行程序提供 [stdio 控制协议](protocol.md)，调用此类型化核心。

## 会话与请求

创建时指定非零 session ID 和 1–64 个程序的固定容量。控制器必须保证跨重连 session ID 唯一，并串行调用所有会话 API。session ID 用于识别错误会话，不提供身份认证。会话初始 generation=1。

请求带 version=1、struct_bytes、session_id、非零 sequence、method 和 expected_generation。sequence 必须严格递增；允许跳号，不允许回绕。版本/大小/方法/会话/sequence 无效时拒绝，不消费序号。合法请求信封一旦受理即消费序号，即使随后发现旧 generation、容量不足或坏包。因此重试相同序号得到 CONFLICT；客户端需要新的 sequence，并先查询状态，不能假设失败请求可重复提交。序号耗尽需要新会话。

| 方法 | 行为 |
|---|---|
| STATUS | 返回当前 generation、最后已受理序号、程序数量、closed；忽略 expected_generation |
| LIST | 与 STATUS 相同的受理规则；传输层在同一次串行处理内枚举程序，返回一致的列表 |
| LOAD | 校验并复制包及可选源码 pin，再发布程序；返回会话内不复用的 program ID |
| UNLOAD | 按 program ID 移除注册表拥有的引用 |
| CLOSE | 释放全部注册表引用，进入终态；后续请求返回 CLOSED |

三个修改方法要求 expected_generation 精确匹配，成功后 generation 加一；失败不改变 generation 或程序集合。回复包含当前状态快照，只有成功 LOAD 才返回非零 program_id。generation 耗尽时拒绝修改，仍可查询并销毁会话。

LOAD 只注册不可变 Program，不执行程序，也不等同于 attach。`fg_session_program` 返回独立保活引用，调用方用 `fg_program_release` 释放。UNLOAD/CLOSE/destroy 不使已经取得的引用失效；持有这些 Program 的 Mount 由目标控制器另行解绑、关闭和排空。没有隐式全局活动程序。

`fg_session_next` 从大于 after_id 的最小程序 ID 开始枚举（首次传 0），每项返回独立引用；NOT_FOUND 表示结束。所有失败清空输出。完整遍历期间必须排除修改，才能得到同一 generation 的一致列表；stdio Agent 的串行处理已满足此条件。顺序由 ID 决定，与内部槽位复用无关。

销毁需要外部排除所有会话 API 调用；stdio Agent 在 EOF 时销毁会话并释放注册表。stdio 传输层当前没有跨连接恢复、请求响应缓存、远端挂载命令、采集通道、SSH 或 GDB 仲裁；目标进程内的 C Mount API 见下文。

## 验证

配置 runtime 的完整 C 包/VM 测试后，执行：

```sh
cmake --build BUILD --target session_test
ctest --test-dir BUILD -R session --output-on-failure
```

测试使用真实 `.fgo`，覆盖加载、错误 pin、截断包、满容量、重复序号、旧代际、未知 ID、身份隔离、序号耗尽、卸载后重用槽位与新 ID、关闭、输入内存释放，以及会话销毁后的独立引用生命周期。

## 目标进程挂载

[`mount.h`](include/fingolfin/mount.h) 提供 `FgMount`，将会话中的 Program、Attachment、binding 和专属 Port 组成明确拥有生命周期的目标侧对象。创建时指定 program ID、入口函数、可信模型 adapter 的 hook ID/native frame 大小/encoder、预算、Host 配置和可选 gate。`attachment.identity.program` 必须等于 program ID，其余配置与对象身份由控制器分配。Program ID 是会话内身份，不应跨会话直接用于共享状态索引。

创建先验证并分配全部资源，最后发布初始路由（generation=1）；失败不返回半成品。`fg_mount_runtime` 返回借用的 SDK v2 表，供目标 adapter 调用。encoder/predicate 代码在 mount 销毁前必须保持加载；数据快照由底层 Attachment/binding 复制。

`fg_mount_detach` 是幂等、非阻塞、终态操作：关闭新 acquire，保留旧租约执行能力。`fg_mount_detached_and_drained` 在全部租约与路由回调退出后为真；随后排空输出并销毁。销毁之前还必须由宿主排除所有 SDK 表调用，包括失败或 disabled 的 acquire，不能仅凭租约数量推断指针无人使用。销毁前未 detach 或未排空返回 BUSY，并保留对象。状态/输出可在 detach 后查询，destroy 不隐式刷新未读记录。

Mount 通过 Attachment 独立保活 Program；registry unload/session destroy **不会隐式 detach**。目标控制器拥有 mount，必须负责 detach/drain/destroy，不能把会话 EOF 当作目标资源已释放。这是供目标控制器使用的 C 原语，尚未接入 stdio Agent 的远端挂载命令；独立 Agent 进程目前没有模型对象可挂载。

`fg_mount_create_many` 支持在同一专属 Port 一次发布 1–64 个订阅，单入口 create 委托此接口。各项必须具有相同 hook、完整 ObjectKey 和 native frame 大小；模型注册方还必须保证 native 字段布局一致，字节大小相等并不能证明布局等价。所有 Attachment/VM/binding 准备成功后才发布路由，否则整批清理且输出对象为空。输入次序决定订阅次序，不隐式去重；每项拥有独立 Host/状态，即便引用同一 Program 或相同 configuration 身份也不合并状态。

一个 SDK factory 构造共同帧，各订阅独立门控。全部已知 false 时跳过构帧；部分失败通过 SDK report 报告，不回滚此前 helper 或其他订阅。`fg_mount_count` 返回订阅数量，`fg_mount_counter_at`/`fg_mount_pop_at` 按输入下标访问状态和输出；原 counter/pop 操作下标 0。detach 一次关闭整组的新获取，旧租约仍可完成全部已选择订阅。

当前支持新建单 Port 的原子初始发布，以及下面的已配置订阅启停；尚未实现对已有活动 Port 增加新入口、更换程序、多 Port 整包原子挂载或跨入口共享状态。测试 `mount` 使用编译器生成 encoder 和真实 VM，验证注册表卸载后存活、持租约 detach、状态/输出排空及失败创建清理，见 [单入口证据](../reports/c-mount.md) 与 [多订阅证据](../reports/c-mount-batch.md)。

### 运行中选择订阅

`fg_mount_select(mount, expected_generation, enabled_mask, &published_generation)` 按创建时的下标选择订阅，bit i 控制第 i 项。初始全部启用，route generation=1。旧代际、范围外 bit 或资源不足均拒绝，输出 generation 不变。即使 mask 相同，成功调用也发布新代际。mask=0 发布空路由，跳过构帧但可再次启用；它与终态 detach 不同。

新代际通过 `fg_binding_clone` 复制 encoder、不可变 gate/facts 和独立 scratch，复用原 Attachment/Host。因此停用与重启保留状态；旧租约仍使用旧选择集合及 route generation。控制 API 需要串行，SDK 调用可并发；Attachment 的执行互斥仍可能返回 BUSY，由 SDK report 显示。

资源有界：最多八组替换快照（包含当前组），另保留初始 binding 模板。`fg_mount_collect` 回收已退役且无人持有的 binding；BUSY 表示部分旧组仍被租约保活，不影响当前路由。select 会先尝试回收；满时返回 CAPACITY，当前路由保持不变。旧租约释放后可回收并重试，不强制终止调用。detach/drain/destroy 会清理所有组。

select **不刷新模型事实或配置身份**，不能当作属性更新安全点。模型发生变化时仍需失效/重新采集与宿主安全点协议。它也不增加新入口或替换 Program。验证包括 500 次发布与两个线程的 4000 次观测、状态/输出核对及 TSan，见 [记录](../reports/c-mount-select.md)。
