# 生成 BPF 的真实执行

需要 Cargo 依赖已缓存、Clang bpfel target、固定版本 uBPF checkout 与已构建的 libubpf.a。先按 ../ubpf/run.py 构建并验证 uBPF。然后在项目根目录运行：

```sh
python3 integration/generated-bpf/run.py --ubpf /path/to/ubpf --build /path/to/ubpf-build --output /tmp/new-generated-bpf-run
```

输出目录必须不存在。脚本核对上游 commit，构建 CLI，从 arithmetic.fg 构建每个函数的开发版 .fgo，经 explain 验证后提取 ELF 与宿主帧头，编译 runner，再将 safe interpreter 结果与 Rust 参考执行器比较。包括 336 组算术/控制流结果和 27 组预算、深度、短帧错误检查，共 363 组。原始输入、预期、结果和命令记录写入输出目录。

runner 的测试帧限定两个 64 位参数，仅用于此测试矩阵。生成器本身也可生成其他检查过的原生布局；这些布局的完整 VM 覆盖仍待补齐。使用解释器 safe profile 和独立 10000 指令限制，不依赖 JIT 安全性。

调用帧是开发格式：steps/max_depth 为输入，error/remaining/result 为输出。入口的模式绑定仍是附加显式参数，必须由调用方先匹配。此路径不替代正式 SDK hook frame、对象生命周期和 cmodel .fg 集成。
