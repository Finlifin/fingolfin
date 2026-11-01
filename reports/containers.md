# 有界容器与状态验证

日期：2026-11-01。

新增 util::containers：BoundedVec、BoundedMap、BoundedSet、BoundedQueue、Counter、Histogram；state::Store 以完整程序/配置/对象身份隔离。容器预分配，拒绝增长越界；FIFO 覆盖必须显式选择并返回被驱逐值。Map 使用排序数组，避免 BTreeMap 的逐节点堆分配，代价是 O(n) 插入/删除。

验证：101 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。新测试覆盖 5,000 次 Map 与 BTreeMap 对比、容量/零容量/重复 key、队列回绕、错误保留输入、计数与 histogram 事务溢出、配置/对象复用以及 10,000 次状态清理重建。

独立测试可执行文件安装计数 allocator，对预分配后的 10,000 轮 Map/Set/Vec/Queue/Histogram/Store 固定大小数据操作计数，alloc/alloc_zeroed/realloc 总计为 0。此证据仅针对容器操作，不代表整个 Port::observe 或 VM 路径已实现零分配。

examples/bounded_state.rs 实际运行路由回调，得到 bytes=512、buckets=[2,1,1]、evidence=[256,64]、evicted=2。原始日志：containers-tests.log、containers-clippy.log、bounded-state-example.log。

当前没有 .fg 语言容器声明和 VM helper；这些接口、字节预算和状态发布/卸载自动化仍需实现。
