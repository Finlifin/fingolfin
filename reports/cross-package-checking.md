# 跨包前端检查记录

2026-11-01，当前未提交工作树。新增 `compiler::check_source/check` 和 `check FILE --model DIR [--model-hash HASH]`。

程序与模型保持独立作用域和符号身份。模型先独立通过检查，防止误捕获程序中的名字；随后依赖索引的模块作用域加包前缀，原声明 ID 不变。程序禁止注入 object/hook 定义。检查结果包含目标 profile、两份源码哈希、模型名称/版本、入口及调用图。

新增 7 个测试：跨包类型与绑定、schema 注入/同名类型拒绝、模型 helper 无法捕获程序名字、私有边界、模型 helper 词法作用域、CLI 双入口与 hash pin、未实现输出 helper 的明确诊断。共 41 个测试；最终日志在 cross-package-tests.log 和 cross-package-clippy.log。

限制：目前仅支持单文件程序依赖一个已解析模型包。传递依赖加载明确拒绝，待实现锁图；源码 hash 不是 ABI/schema fingerprint。check 尚不生成代码，不证明执行预算、缓存失效或并发安全。原始输出示例依赖的 emit_pages 尚未实现，不能被静默替换为无副作用 stub。
