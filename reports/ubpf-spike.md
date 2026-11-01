# uBPF 技术探针

锁定 commit：8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521。源码 `/tmp/fingolfin-ubpf`，构建 `/tmp/fingolfin-ubpf-build`。原始命令和结果见 ubpf-spike.log。

复现：

```sh
python3 integration/ubpf/run.py --ubpf /tmp/fingolfin-ubpf --build /tmp/fingolfin-ubpf-build --log reports/ubpf-spike.log
```

Clang 23.1.4 编译 `bpfel -mcpu=v3 -O2 -g`；native probe 链接固定 uBPF 静态库。成功项包括 ELF 加载、固定 ID helper、有界循环、局部函数、32 组解释器/JIT/宿主公式一致性、未注册 helper 拒绝、解释器指令预算和越界拒绝。safe profile 的越界拒绝和 JIT 不支持行为也已验证。

明确限制：Clang 23 为外部符号 helper 生成 src=1 的 call 重定位；当前 uBPF 加载器按局部函数解析，拒绝该 ELF。探针保留此已知失败用例；成功路径使用固定 helper ID，不宣称按名称 helper 重定位通过。后端代码生成须显式声明 helper ID ABI，不能静默依赖按名称重定位。

uBPF 指令预算 API 仅作用于解释器；safe profile 当前仅解释执行。JIT 的预算和内存安全尚无相同证明，不可据正常输入输出一致性宣称已完成安全验收。尚未最终冻结 VM 选型，也没有真实 tt-emule 调用此 VM。
