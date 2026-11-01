# .fg 状态与输出 helper 验证

```sh
python3 integration/state-helpers/run.py --ubpf /path/to/pinned-ubpf --build /path/to/ubpf-build --output /tmp/new-state-helper-run
```

脚本要求输出目录不存在，核对固定 uBPF commit，构建 record.fg 的开发版 .fgo 并用 explain 验证，再执行包内 ELF。runner 注册 counter_add=1 / emit=2 的 allowlist 和 safe scalar-return metadata；使用 safe interpreter 与独立指令预算。

测试使用每次调用 cookie 定位各自 State；无 TLS 或全局当前程序。C runner 的固定槽/两条输出存储用于独立验证，Rust 宿主库实现与完整 loader 尚未直接链接。覆盖交替状态实例不串线、未知槽、计数溢出、输出满和缺少 helper 注册拒绝。输出满前已成功执行的计数累加保留，不提供整次入口回滚。

此接口仅输出 u32 tag / u64 value；不是完整输出 schema 或全部语言容器实现。
