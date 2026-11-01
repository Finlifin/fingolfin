# .fg map/histogram 聚合接口

日期：2026-11-01。

实现 builtin.map_add(u32,u64,u64) 和 builtin.histogram_add(u32,i64,u64)，helper IDs 3/4。Intrinsic 参数签名改为静态类型切片，Host 提供固定五寄存器 ABI 与 Value 参数校验，支持预声明有界 map/直方图。每次操作不创建新的容器 slot。

CLI eval 新增 --host-config，读取最多 1 MiB 的严格 JSON；支持 counters/maps/histograms/output_capacity，验证同类重复 slot、边界、容量与分配失败。程序结束后可读取完整状态快照，快照分配与热路径分开。

验证：109 个 Cargo 集成测试、Clippy warnings-as-errors、格式检查通过。10,000 轮分配计数测试扩展到四种 helper 的固定状态操作，仍为 0 次 alloc/alloc_zeroed/realloc。参考执行测试覆盖 map 满、键累加溢出、histogram 分桶、ABI 参数拒绝、配置校验和 ELF helper 清单。

真实 VM 测试使用新 .fgo 与 uBPF safe interpreter，验证两个独立状态、i64 极值与边界分桶、容量、map 溢出和 histogram 总量溢出。旧 counter/emit VM runner 也重新通过。新证据在 aggregate-helpers-1/；全量日志为 aggregate-helper-tests.log、aggregate-helper-clippy.log。

限制：只提供计数累加 map 和加权直方图，尚无语言容器声明、通用 map 读取/删除、循环迭代或完整输出 schema。Rust Host 与正式 VM loader 仍未直接链接；C runner 为独立适配测试，不代表 FNG-018 完整交付。
