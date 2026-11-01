# Map / histogram helper 的真实 VM 验证

```sh
python3 integration/aggregate-helpers/run.py --ubpf /path/to/pinned-ubpf --build /path/to/ubpf-build --output /tmp/new-aggregate-helper-run
```

脚本锁定 uBPF revision，编译 aggregate.fg 的 .fgo，验证 ELF helper IDs=[3,4]，然后在 safe interpreter 注册 scalar-return metadata 并执行。C adapter 使用独立调用 cookie 定位状态，不使用 TLS/global 当前程序。

覆盖两实例交替采样、i64 最小/最大采样值、边界 0/10、有界 map 满、map 计数溢出、histogram total 溢出，失败时保持对应容器原值。先成功的 map 更新在后续 histogram 失败时保留。该 C adapter 是独立固定容量测试实现；完整 Rust Host↔VM loader 桥仍待接入。
