# 调用帧 schema v1 与开发包 v3

先读 [ABI 心智模型](abi-model.md) 理解 native frame、BPF frame 和 schema 的关系；本章用于查二进制细节。

Rust 编译器保留 `.fg` 前端；用户态 C 不依赖 CheckedProgram 或解析 C 头文件获取帧布局。开发 `.fgo` version 3 为每个 Entry 增加 `frame_schema` 字节数组及 `frame_schema_hash`（SHA-256）。原 frame_header、signature 与各自哈希保留；外层完整性涵盖全部字段。`explain` 显示 schema 字节数与哈希。

version 1/2 开发包必须重建。此版本仍为 `development-function-frames`，可由当前 C Session/Mount 接入链加载，但不是承诺稳定兼容的正式发布格式；状态/输出 schema、simulator 身份和完整模型契约仍未齐备。

## Wire 格式

所有整数为 little endian，无 native 指针。文件顺序固定：64 字节头、N 个 40 字节类型节点、M 个 24 字节成员、S 个 24 字节 slot、连续 UTF-8 名字字节。没有结构体隐式 padding。输入可以非对齐。

| 头部 offset | 类型 | 内容 |
|---|---|---|
| 0 | 8 bytes | `FGFRAME\0` |
| 8 / 12 | u32 / u32 | schema version=1 / 文件精确长度 |
| 16 / 24 | u64 / u32 | frame 字节数 / alignment |
| 28 / 32 / 36 | u32 × 3 | parameter 数 / binding 数 / result 是否存在（0 或 1） |
| 40 / 44 / 48 | u32 × 3 | node 数 / member 数 / 名字区字节数 |
| 52 / 56 / 60 | u32 × 3 | 保留 0 / slot 数 / 保留 0 |

| 类型节点 offset | 类型 | 内容 |
|---|---|---|
| 0 / 1 / 2 / 3 | u8 × 4 | kind / signed / bits / 保留 0 |
| 4 / 8 | u32 / u64 | alignment / size |
| 16 / 20 / 24 / 28 | u32 × 4 | array element node / member 起点 / member 数 / 保留 0 |
| 32 | u64 | array length |

kind：1=integer，2=bool，3=opaque pointer，4=enum，5=record，6=array。无关字段必须为 0。integer/enum 支持 8/16/32/64 位，signed 为 0/1；bool 为 size=align=1；pointer 为 size=align=8。pointer schema 不授权解引用，也不记录借用生命周期。record 按声明布局中的 offset 排序，array 引用元素节点与长度。

每个 member：offset 0 为 u64（record 字段偏移，或 enum 判别值的 64 位二进制补码）；offset 8 为 u32 child node（enum 为 UINT32_MAX）；offset 12/16 为名字区 offset/length；offset 20 保留 0。枚举判别值必须在其固定表示范围内；同一类型的名字和 enum 判别值不能重复。

每个 slot 使用相同 24 字节记录：offset 0 为 frame 偏移，8 为 type node，12/16 为名字 offset/length，20 为 role（0=parameter，1=binding，2=result）。参数按位置命名 `arg0`、`arg1`；绑定按名字排序；返回槽命名 `result`。顺序为 parameters → bindings → result。frame 的前 16 字节固定为 steps:u64、max_depth:u32、error:u32，结果与 padding 在调用前清零。

## 验证不变量与资源限制

类型节点按后序排列，child 必须早于 parent。节点组成无共享的森林：每个节点恰好被一个 parent 或顶层 slot 使用，无循环、共享子树或孤立节点。成员区按节点顺序连续分配，名字区按全部 member 然后 slot 的顺序连续存放，禁止空洞、重叠或未引用尾部。

名字为非空、无 NUL 的 UTF-8。schema ≤1 MiB、节点/slot ≤4,096、单个类型成员 ≤256、嵌套深度 ≤32、单名 ≤4,096 字节、frame ≤32 MiB。这些是此 schema profile 的明确资源上限；超限拒绝，不能静默截断。

record 字段按各自 alignment 精确排列，最终 size 必须等于尾部对齐结果；array size 必须精确等于元素 size×length。顶层同样检查偏移与尾部对齐。C 校验无堆分配，以固定上限数组记录节点使用和深度。Rust 编译器校验同一契约，跨语言差分测试覆盖合法与损坏输入。

schema 描述结构 ABI。名义类型身份仍由原 signature/模型身份契约承载；结构校验及哈希本身不证明 schema 与任意外部 ELF 的行为一致，也不证明来源可信。loader 必须从同一个经过身份/完整性检查的包选择对应 ELF 与 schema。

## C API

[真实接口](../runtime/include/fingolfin/schema.h)：

- `fg_frame_schema_check` 返回借用只读 view；失败不改变输出。
- `fg_frame_schema_slot/type/member` 提供布局及名字视图。仅可用于已校验 view，底层字节必须保持不变并存活。
- `fg_frame_schema_init` 要求精确 frame 大小，清零数据和 padding，初始化 steps/max_depth；不分配。

C package loader 已接入开发 v3 包解析；Session/Mount 可选择包内入口及对应 ELF/schema，生成 encoder 完成 native 到 BPF frame 转换并验证输入。C VM 测试也可直接读取离线提取的 `.schema`/ELF 构帧执行，这是一条测试路径，不是所有用户都必须手动拆包。完整状态/输出 schema 和稳定发布兼容契约仍未完成。

## 复现

`cargo test --test frame_schema -- --nocapture` 编译独立 C 校验器，生成并比较 Rust/C 语料；输出其临时 corpus 路径。用 CMake 的 `FG_SCHEMA_CORPUS=/path/to/corpus.bin` 可将该语料加入 CTest，在 sanitizer 构建中重放。默认 CTest 的 schema 测试不依赖 Rust，包含边界检查和 10,000 次非对齐变异。

生成新版 VM fixture 继续使用 [集成脚本](../integration/c-runtime/README.md)，现在会额外输出 `.schema`。已有 fixture 目录需重新生成至新目录。
