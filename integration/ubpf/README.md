# uBPF 集成

使用 [upstream.lock.toml](upstream.lock.toml) 锁定的 checkout。基础探针由 `run.py` 驱动；预分配安全解释器 scratch 使用 [safe-scratch-v1.patch](patches/safe-scratch-v1.patch)，准备脚本为 `prepare-scratch.py`。外部源码和构建目录不在本仓库内。

```sh
python3 integration/ubpf/run.py --help
python3 integration/ubpf/prepare-scratch.py --help
```

C 运行时启用 `FG_WITH_UBPF` 时检查 checkout revision 和 `lib/libubpf.a`。启用 `FG_UBPF_SCRATCH` 还要求源码已应用上述补丁。真实 tt-emule 的统一重建会在新目录构建已准备的 uBPF，详见 [验证说明](../tt-emule/VERIFY.md)。

探针验证不能替代完整模型接入；C Host/VM 和 SDK/Mount 的使用见 [C 接入指南](../../docs/c-integration.md)。
