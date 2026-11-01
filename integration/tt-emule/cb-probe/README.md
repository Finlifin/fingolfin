# 真实 CB 技术探针

这是 M0 实验，不是正式 adapter 或 `.fg` 后端。应用 hooks.patch 到锁定 tt-emule 后，无需重编 host binary，新的 JIT kernel 会从预加载库解析 weak C 回调。

```sh
# 先 git apply --check，避免重复应用。
git -C /tmp/fingolfin-tt-emule apply /home/fin/apps/fingolfin/integration/tt-emule/cb-probe/hooks.patch
cmake -S /tmp/fingolfin-ubpf -B /tmp/fingolfin-ubpf-build -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build /tmp/fingolfin-ubpf-build -j 2
mkdir -p /tmp/fingolfin-cb-probe
clang -target bpfel -mcpu=v3 -O2 -g -c integration/tt-emule/cb-probe/program.bpf.c -o /tmp/fingolfin-cb-probe/program.o
clang -shared -fPIC -std=c11 -Wall -Wextra -Werror -I sdk/include -I /tmp/fingolfin-ubpf/vm/inc -I /tmp/fingolfin-ubpf-build/vm integration/tt-emule/cb-probe/runtime.c /tmp/fingolfin-ubpf-build/lib/libubpf.a -pthread -lm -o /tmp/fingolfin-cb-probe/runtime.so
python3 integration/tt-emule/cb-probe/run.py --metal /tmp/fingolfin-tt-metal --build /tmp/fingolfin-emule-build --probe /tmp/fingolfin-cb-probe --output reports/NEW-CB-PROBE-RUN
```

使用新的 output 目录，脚本不会覆盖既有证据。两个场景各运行 on/off/miss，比较 gtest XML 与精确 frame、VM 和页数计数。关闭/不匹配时 VM 可以已加载但不执行，frame 不构造。报告由真正收到 CB 事件的进程写入，避免 LD_PRELOAD 传播到编译器子进程时覆盖证据。

目前 hook owner 限定 core(0,0)/CB0，选择模式 miss 则匹配 CB16。run/launch/generation 固定为 1；不可据此推断完整对象复用或生命周期安全。运行时实例 mutex、首次事件初始化仅用于实验，不满足最终性能目标。
