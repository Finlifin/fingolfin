# 公共 util

编译器工具与目标端基础工具分别实现，避免 C 用户态链接 Rust。

## C 目标端

| 头文件 | 职责 |
| --- | --- |
| util.h | FgStatus、allocator、checked size/对齐、counter、预算 |
| bytes.h | 有界字节视图与读取 |
| elf.h | BPF ELF 前置结构校验 |
| schema.h | 机器可读调用帧布局、参数和值验证 |
| objects.h | 有界对象身份、父子关系与代次 |
| facts.h / gate.h | 复制事实、预算化读取、三值谓词与阶段门控 |

预算不足保持余额不变，调用方必须传播失败。包、ELF 和 schema 的结构校验不等同于程序来源认证；VM 指令校验仍独立执行。

## Rust 编译器

| 模块 | 职责 |
| --- | --- |
| util::budget | 显式工作额度 |
| util::artifact | 同目录临时文件与原子发布 |
| util::text | UTF-8 字节位置到行列映射 |
| util::hash | SHA-256 与长度前缀内容哈希 |
| util::elf | ELF 校验和实际 helper ID 提取 |
| util::containers | 有界容器参考实现 |

模型内容哈希用于精确源码身份，不代替 ABI 兼容性或身份认证。语法源位置按字节记录，不能把字符索引直接当字节偏移。

容器详见 [容器与状态](runtime-containers.md)，目标接入见 [C 接入](c-integration.md)。

## 状态码按调用层解释

| 状态 | 一般意义 | 调用者应保留的信息 |
| --- | --- | --- |
| INVALID | 参数、结构版本、身份或布局不合法 | 哪个 API、输入尺寸、identity |
| OVERFLOW | 算术或计数无法表示 | 原值与请求增量/尺寸 |
| CAPACITY | 预分配空间不足 | 容器/资源种类及初始容量 |
| NOMEM | 创建资源时分配失败 | 创建阶段，已拥有的资源 |
| NOT_FOUND | slot/key/entry 不存在，或队列已空 | 是否是预期的遍历终止 |
| BUDGET | 本次工作额度不足 | 对应预算层及配置 |
| VM | 执行层失败 | VM 结果与附带诊断 |
| BUSY | 并发执行或仍持有引用 | 需要等待的执行/lease |
| CLOSED | 已停止接收新操作 | 所有者生命周期状态 |
| CONFLICT | sequence/generation 等冲突 | 预期与实际代际 |
| IO | 输入输出失败 | 路径/阶段与系统诊断 |

使用 `fg_status_string` 打印名称，同时记录失败 API。SDK v2 使用自己的 int32 返回约定，不能把任意负值直接当 FgStatus 传入。VM 返回状态和 Host helper 错误也需分别解释，详见对应公共头文件。

## checked size 和预算

计算 `元素数 × stride + header` 时先调用 `fg_size_mul` 再调用 `fg_size_add`，不要先执行可能溢出的 C 算术再检查。`fg_size_align` 用于合法对齐；创建容器还会验证 element_size/alignment 等布局要求。

`fg_counter_add` 和 `fg_budget_charge` 的失败都保持原值。预算失败后不能继续操作然后当作已扣款，也不能把余额设零来掩盖调用顺序错误。每个 API 的错误应返回至定义了停止/重试策略的层。

## 字节视图不拥有内存

FgBytes 是指针与长度的借用视图。`fg_bytes_sub` 检查范围并生成子视图；u16/u32/u64le 读取小端值，避免未对齐的原生整数加载。函数不会验证任意指针是否真的指向可读内存：调用者必须保证整个非空 span 有效且活到读取结束。

失败保持输出参数不变，因此调用者必须先检查状态再使用输出；保留的旧值不代表本次读取成功。包加载采用复制所有权时可释放原 bytes，但普通子视图不能比原存储活得更久。

## 自定义 allocator 的寿命

allocator 描述可在 create 时复制，但其 context 及回调代码必须持续到所有使用它的对象销毁。allocate 应返回满足请求对齐的内存或 NULL，deallocate 应对应同一分配约定。回调不能重入修改当前对象。分配次数监控可用于检查创建阶段资源预算；容器 mutation 不分配内存，不代表整个 Mount 首次建立也无分配。

## 为什么区分结构验证和可信执行

ELF/schema 校验拒绝越界、格式和布局问题；内容哈希检测输入身份变化；这些都不认证作者或授予访问目标的权限。控制器仍要提供可信 hook 映射、允许的 helper 集合和预算。Program LOAD 成功也仍需为具体对象建立 Mount，才能进入目标执行链。
