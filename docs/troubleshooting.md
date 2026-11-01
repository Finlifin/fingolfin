# 排错手册

先保留失败命令、退出码、输入源码/模型和输出目录。编译阶段没有生成产物时，不要继续拿旧 `.fgo` 测运行；目标计算通过但观测失败时，也不能把二者合并成一次成功。

## 构建与生成

| 现象 | 检查方法 | 处理 |
| --- | --- | --- |
| Rust edition 不支持 | `rustc --version` 与 Cargo.toml | 使用支持 edition 2024 的工具链 |
| Clang 不支持 bpfel | 查看 emit/build 的 Clang 诊断 | 使用带 BPF backend 的 Clang；普通 C 编译成功不代表支持 BPF |
| CMake 找不到 json-c/OpenSSL | 检查 pkg-config 与开发文件 | 安装开发依赖，或只构建不需要 package 的基础库 |
| uBPF revision 拒绝 | `git -C PATH rev-parse HEAD` | 使用锁定 revision；不要删除 pin 校验 |
| 找不到 libubpf.a | 检查 `FG_UBPF_BUILD/lib/libubpf.a` | 传构建根，先完成 uBPF build |
| scratch 符号缺失 | 检查源码补丁与实际链接库 | 对 patched checkout 重新构建，勿混用旧静态库 |
| 共享库 relocation/PIC 错误 | 检查 uBPF/runtime 构建配置 | 使用 `CMAKE_POSITION_INDEPENDENT_CODE=ON` 重建 |
| CMake 要求 record.o/schema/fgo | 当前是否启用 BUILD_TESTING | 先运行 fixture prepare，填齐测试输入 |
| prepare/output 已存在 | 核对是否重复运行 | 使用新目录，避免覆盖已有证据 |

## 语言和模型

`parse` 成功只证明语法可解析。用 `check SOURCE --model MODEL` 做程序与模型检查；`inspect --types` 侧重模型类型和契约，`inspect --bodies` 加入函数体检查。

hook 签名必须与模型的参数顺序、规范类型一致。`u32` 和 `u64`、指针和按值记录不可互换。`builtin.emit` 的 tag 为 u32、value 为 u64，不能把任意 u32 变量直接当成 u64 参数而期待隐式提升。

`examples/fixture/cb_watch.fg` 含未实现的 `emit_pages`，用于展示语法；可运行起点是 `tests/fixtures/valid_observation.fg`、`integration/state-helpers/record.fg` 或 `examples/tt_emule/device_watch.fg`。

availability 使用精确路径，大小写和路径层级都重要。未声明属性默认 Invocation；父 record 可用不意味着其所有字段自动提前可用。不能穿过标量继续访问字段，也不能靠别名绕过模型契约。参考 [属性阶段](model-availability.md)。

## 包、Session 与 Mount

包校验失败时先执行 `fingolfin explain FILE.fgo`，再对比 C 加载诊断。开发包、schema、入口名、模型哈希和生成 adapter 必须对应；不要靠手改 manifest 哈希“修复”身份错误。

Session CONFLICT 常来自 sequence 重放或 expected_generation 过期。合法 envelope 一旦被接受，即使后续包校验/容量/代际检查失败，也会消耗 sequence；重试需要新的 sequence。generation 只在成功修改时递增。STATUS/LIST 可读取当前状态，但不是重放缓存。详细规则见 [协议](../agent/protocol.md)。

Mount create INVALID 时逐项检查 program_id 与 identity.program、完整对象键、native frame 大小、encoder、Host 版本和 helper mask。NOT_FOUND 可能是函数名或 Program 不存在，不能只根据状态名判断来源；保留调用位置和 diagnostic。

## 运行中没有输出

按顺序判断：

1. 原始目标 API 是否真的执行？检查宿主测试是否跳过、测试过滤器是否选中。
2. acquire 是否成功且 enabled？已 detach、错 hook、旧对象键会在 factory 前被拒绝。
3. 属性 gate 是否筛掉？filtered 是正常选择，不是执行错误。
4. factory 是否执行？多个订阅仍只构造一次 frame。
5. report 的 attempted/failed 是多少？部分订阅可能失败。
6. 程序体是否真的走到 emit？body 的 if 与提前 gate 是不同层次。
7. Host 是否配置对应 slot、输出是否已满、预算是否耗尽？
8. 记录是否已被其他消费者排空？

不建议在缺失值时补零来让 predicate 通过。未知属性应保持 unknown；正式 tt-emule Object 策略无法确定真假时拒绝挂载。

## 容量、BUSY 与部分提交

CAPACITY 表示有界资源不足，处理取决于来源：新 map key 可扩大初始容量；输出 FIFO 可增加排空频率；路由替换快照需等待旧 lease 释放并 collect；对象注册表需检查是否漏退役。不要对所有 CAPACITY 使用同一无限重试循环。

BUSY 可能是 Attachment 正执行、仍有 lease/pin 或退役快照未释放。destroy BUSY 保留对象，调用方应保留所有权后重试。不能先 free 结构，再等待内部引用归零。

helper 操作失败保持该容器原值，但先前成功 helper 不回滚。若 emit 失败而 counter 增长，应先看执行顺序，不要立即判断容器损坏。对一次观测必须原子提交多个结果的应用，应先设计额外事务/缓冲机制；当前 API 不提供这种保证。

## tt-emule 的特殊问题

| 现象 | 优先排查 |
| --- | --- |
| catalog 校验失败 | 实际探针是否完整、revision 是否一致、是否拿了历史 catalog |
| 39 个 hook 未全部命中 | 场景补丁是否安装、gtest 是否实际运行、日志是否显示跳过 |
| device 错误或宽度截断 | v2 探针/launch 补丁、Core ABI、是否重建了全部分组 |
| 修改探针后仍像旧行为 | JIT cache key 是否包含新探针头、是否混用旧 DSO |
| 两设备出现相同状态 | ObjectKey 是否包含 device 对应对象身份，是否误共享 Host |
| filtered 和预期相反 | 实际 policy 源码、构建时快照、运行器 expected-device 是否一致 |
| teardown BUSY | 是否存在持有 lease 的线程；旧对象树必须保留到排空 |
| HTML 有数据但验证退出失败 | 查看 stages 的失败步骤，不能只看已完成页面 |

历史测试中的 `riscv_wait`/指针宽度或 double-free 失败记录不属于通过覆盖。不要因为有 39 点成功的新场景，就推断所有旧上游测试已经修好。

## 准备可复现的问题记录

包含 Fingolfin 提交、编译器版本/哈希、模型路径、最小 `.fg`、完整命令、stderr、CMake 选项和退出码。tt-emule 另附 inputs、stages、catalog、失败场景日志及外部补丁。输入有私有数据时先制作不含私有内容的最小案例，仍保留参数类型、容量和生命周期顺序。
