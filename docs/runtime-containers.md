# 有界容器与状态

目标端使用 C11 API，头文件为 `runtime/include/fingolfin/containers.h` 和 `state.h`。创建时分配容量，操作不自动扩容；使用者提供独占可变访问与资源生命周期管理。

| 类型 | 行为 | 复杂度 |
| --- | --- | --- |
| FgVec | 固定大小/对齐元素，push/pop、索引、clear | 索引和尾部操作 O(1) |
| FgMap | 有序数组，二分查找，满时仍可更新已有键 | 查找 O(log n)，插入/删除 O(n) |
| FgSet | 使用零大小 value 的 map，重复键不增加长度 | 同 map |
| FgQueue | 固定容量 FIFO；普通 push 拒绝满，overwrite 显式返回旧元素 | O(1) |
| FgHistogram | 固定递增 i64 边界，u64 权重与总量 | 定位 O(log n) |
| FgStateStore | 按完整 FgStateKey 隔离固定布局值 | 有界扫描与清理 |

## 所有权和错误

元素按字节复制，不调用析构，也不深拷贝外部指针。借出的 view 在 mutation/destroy 后失效；map key 不得原地修改。比较器必须给出稳定全序，不能重入修改容器。输入输出不得与容器内部存储重叠。

容量不足、size 溢出、计数溢出和无效对齐显式返回状态码。Histogram `[0,10]` 对应 `[-∞,0)`、`[0,10)`、`[10,+∞)`；桶或总量溢出不改变已有值。状态 key 包含 program/configuration/run/launch/slot/generation，复用 slot 不能复用旧状态。

## `.fg` helper

| 调用 | helper ID | 行为 |
| --- | --- | --- |
| builtin.counter_add(slot, amount) | 1 | 更新已声明 u64 counter |
| builtin.emit(tag, value) | 2 | 写入有界输出 FIFO |
| builtin.map_add(slot, key, amount) | 3 | 累加预分配 map 中的 key |
| builtin.histogram_add(slot, sample, weight) | 4 | 累加固定 histogram |

slot/tag 为 u32，计数/key/value 为 u64，sample 为 i64。Host 配置在执行前声明容量与边界；helper 不隐式创建未知 slot。一次入口内后续失败不回滚此前成功操作。

## 参考与验证

构建与 API 细节见 [C runtime](../runtime/README.md)。Rust 的 `src/util/containers.rs` 和 `src/state.rs` 是参考工具实现；`cargo run --example bounded_state` 演示该参考实现。目标侧契约和测试以 C 头文件及 `runtime/tests/containers.c`、`host.c` 为准。
