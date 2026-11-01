# C ELF loader 前置检查

日期：2026-11-01。

新增 bytes.h/bytes.c：借用字节视图、checked subrange 和显式 little-endian 整数读取；支持非对齐输入，失败不改变输出。

新增 elf.h/elf.c：不分配的 ELF64 little-endian BPF relocatable 结构检查。限制文件 32 MiB、section 32、symbol 65,536、名字 4,096 字节；验证完整表边界、section 对齐/重叠、共享字符串表、符号类型/范围、唯一 global entry、函数/宽指令边界与实际 helper IDs。未知 helper、未定义 symbol、重定位和当前不支持的数据布局均拒绝。

VM loader 在 uBPF 分配/加载之前执行检查并核对 helper mask。字节视图接受任意对齐；由于 upstream 内部读取 native ELF struct，传入 uBPF 前使用对齐的临时拷贝，加载结束释放。当前 profile 是编译器实际生成的无重定位 ELF，不代表完整正式包/schema 校验。

验证：

- 合成合法 ELF、每个截断长度、损坏 section/symbol/string 字段、宽指令/helper 检查；30,000 轮确定性单字节变异使用非对齐地址，未发现越界或 sanitizer 错误。
- 真实生成 record ELF 的每个截断均在 C VM loader 拒绝；非对齐完整 ELF 成功加载并执行 C helper。
- 已有 9 个算术生成 ELF 和本轮沿用的 record/aggregate ELF，共 11 个产物通过 C 检查；旧开发包只提取其既有 ELF，不宣称旧包与当前正式格式兼容。
- 启用 uBPF 的 5 个 CTest 程序在 runtime/uBPF 同时启用 ASan/UBSan 的构建中通过，包含沙箱外 LeakSanitizer；GCC Release 默认 4 个 CTest 程序通过。

原始记录：c-elf-1/。编译后的 byte-view/结构检查无堆分配；这不等于整个 loader 或 VM 无分配。变异测试不是完整 fuzzing 或安全证明；执行仍依赖 uBPF verifier、safe metadata、allowlist 和指令上限。

后续需要完整包 schema、完整性/身份契约、自动模型 adapter 和正式 cmodel 接入。当前拒绝的 relocation/data profile 需要明确契约与验证后才能开启，不能让 upstream 隐式跳过未知 relocation。
