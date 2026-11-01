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

## 选择哪种存储

按索引保存定长采样结果用 Vec；按标量键累计值用 Map；只关心是否出现过用 Set；按到达顺序消费记录用 Queue；统计分布用 Histogram；按程序和对象生命周期隔离任意定长状态用 StateStore。Map 采用有序数组，适合容量可控的观测状态；它不是可无限增长的通用哈希表。

容量应按最坏同时存活对象/键数量计算。队列容量按消费者两次排空之间可能产生的记录数计算，而不是按 hook 数量计算。一个 hook 可执行多次，一次入口也可 emit 多条。满载策略要显式选择：拒绝新元素、明确 overwrite，或在更高层标记批次失败；容器不会悄悄扩容。

## C API 示例：固定向量

```c
#include "fingolfin/containers.h"
#include <assert.h>
#include <stdint.h>

void vector_example(void) {
    FgVec *v = NULL;
    uint64_t input = 7, output = 0;
    assert(fg_vec_create(2, sizeof(input), _Alignof(uint64_t), NULL, &v) == FG_STATUS_OK);
    assert(fg_vec_push(v, &input) == FG_STATUS_OK);
    assert(fg_vec_length(v) == 1);
    assert(*(uint64_t *)fg_vec_at(v, 0) == 7);
    assert(fg_vec_pop(v, &output) == FG_STATUS_OK && output == 7);
    fg_vec_destroy(v);
}
```

NULL allocator 使用默认分配器。create 复制 allocator 描述，失败时 out 为 NULL。元素的外部指针只会被复制；若元素持有资源，调用方必须在清理容器前逐个处理，不能指望 destroy 调用元素析构。

## Map 与 Set 的比较器

比较器返回负数、零、正数，必须定义稳定全序。对 u64 不要写 `return a - b` 再截断成 int，应比较大小返回 -1/0/1。`fg_map_put` 对已有键替换 value，即使 map 已满也允许；对新键才检查容量。可用 `inserted` 区分插入与替换。`fg_map_at` 按键序遍历，不能假设插入顺序。

Set 通过零字节 value 实现，使用 map 的 put/find/remove 并按头文件约定传 NULL value 参数。它仍按字节存储 key，带指针的 key 的有效期和比较语义由调用方保证。

## Histogram 的边界例子

对 bounds `[0,10]`，分别加入 `(-1,2)`、`(0,3)`、`(10,4)`，结果为 counts `[2,3,4]`、total 9。每个括号是 `(sample,weight)`，不是次数和区间。边界必须严格递增，所需桶数为边界数加一；`max_buckets` 至少为 3。样本可为负数，权重为 u64。

溢出检查同时考虑目标桶和 total，任何一项溢出都拒绝该操作并保持原有 histogram。borrowed counts 只在无修改、无销毁时可读；并发读取仍需调用方同步。

## `.fg` 聚合的完整输入

[aggregate.fg](../integration/aggregate-helpers/aggregate.fg) 将 key 的 weight 累加到 map，并将相同 weight 写入 sample 所属桶。配置文件为：

```json
{"maps":[{"slot":0,"capacity":2}],"histograms":[{"slot":0,"bounds":[0,10],"max_buckets":3}]}
```

```sh
target/debug/fingolfin eval integration/aggregate-helpers/aggregate.fg \
  --model models/fixture --function program::aggregate \
  --args '[{"Integer":7},{"Integer":-1},{"Integer":2}]' \
  --host-config integration/aggregate-helpers/config.json
```

应看到 key 7 的累计值 2，histogram 第一个桶为 2、其余为 0、total 为 2。每次独立启动 eval 都重新创建参考 Host，因此重复执行此命令不会变成 4。要验证持久累加，应像 [C 教程](c-walkthrough.md) 一样在同一个 Mount 上执行多次。

C 对应配置使用 `FgMapConfig{slot,capacity}`、`FgHistogramConfig{slot,bounds,bound_count,max_buckets}` 放入 FgHostConfig，并设置 version/struct_bytes；Host 创建时复制边界并分配存储。各类型拥有独立 slot 空间，counter 0 与 map 0 可以共存，同类型重复 slot 不合法。

## StateStore 的清理粒度

FgStateKey 的六部分为 program/configuration/run/launch/slot/generation。按 program 清理适合卸载完整程序；按 configuration 清理适合退役配置；按 object 清理必须包含完整对象代际；按 launch 清理处理整批结束。删除状态前应排空仍可能访问它的 lease。

复用 slot 时 generation 必须改变，不能将旧对象状态迁给新对象。StateStore 只执行指定键的存储与删除，不替模型分配 run/launch，也不自动识别配置迁移。需要迁移时由控制器定义转换过程并在发布前完成。
