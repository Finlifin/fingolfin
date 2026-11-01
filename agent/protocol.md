# Agent stdio 控制协议 v1

启用 runtime 的 `FG_WITH_PACKAGE`、`FG_WITH_UBPF` 后构建 `fg-agent` target，启动 `fg-agent --session-id 42`。启动者必须分配跨重连唯一的非零 u64 session ID。每个进程只有一个会话，容量 64 个 Program。stdin/stdout 为控制流，stderr 为进程诊断。C [`fgctl session`](../cli/README.md) 已支持此接口，编译器远端调用尚未接入。

采用受限 JSON-RPC 2.0 风格：每条请求为单行 UTF-8 JSON，以 LF 结束；支持请求/响应，不支持 batch、notification 或并行请求。JSON 字符串内换行必须转义。每行最多 64 MiB（不含 LF），解码后的 package 最多 32 MiB。超长行排空至 LF，返回错误后继续读取下一行。EOF 销毁会话；没有 LF 的末尾片段不执行，进程返回 1。正常 EOF 返回 0，用法错误返回 2，资源或流错误返回 1。

所有 u64 身份、序号、代际以十进制字符串传输，避免 JavaScript 精度丢失；除 `"0"` 外禁止前导零，禁止符号或空白。请求 ID 同时是会话 sequence，必须非零且严格递增。`version` 为整数 1。

```json
{"jsonrpc":"2.0","id":"1","method":"session.status","params":{"version":1,"session_id":"42","generation":"0"}}
```

顶层字段必须恰好是 jsonrpc/id/method/params；params 的公共字段恰好是 version/session_id/generation，加上下表方法字段。未知字段、重复键（含转义同名键）、NUL 字符串、浮点版本、非法 UTF-8、尾随内容、错误类型均拒绝。

| method | 附加 params 字段 | 含义 |
|---|---|---|
| session.status | 无 | 查询，忽略 generation |
| program.list | 无 | 查询当前程序和入口，忽略 generation；不修改配置代际 |
| program.load | package、program_hash、model_hash | 三者都是字符串；package 是原始 `.fgo` JSON 文本的字符串编码；空 hash 表示不指定 pin |
| program.unload | program_id | 字符串 u64 ID |
| session.close | 无 | 关闭会话并释放注册表引用 |

修改请求的 generation 必须精确匹配；初始为 `"1"`。成功修改加一。完整会话语义、错误请求是否消费 sequence、独立引用与关闭规则见 [会话文档](README.md)。LOAD 只注册已验证 Program，不挂载或执行 hook。

成功响应包含原字符串 id 与 result，例如：

```json
{"jsonrpc":"2.0","id":"1","result":{"session_id":"42","last_sequence":"1","generation":"1","program_id":"0","program_count":0,"closed":false}}
```

只有成功 LOAD 的 program_id 非零。会话操作失败使用 `error.code=-32000`、稳定的 C status 名称作为 message、当前状态作为 data。客户端应使用 code 和 data 决策，不解析人类日志。语法或信封错误统一返回 `id:null,error.code=-32600,message:"invalid request"`，不消费 sequence，也不泄露部分解析的 ID。解析失败不区分 JSON-RPC 的细分 parse/params/method 错误码，因此这是受限协议，不声明完整 JSON-RPC 兼容。

成功 `program.list` 的 result 额外包含 programs 数组，按 program_id 升序排列，最多 64 项。每项含 program_id（字符串）、program_hash、model_hash、entry_count（整数）及 entries 数组。每个入口含 function、hook（普通函数为空字符串）、frame_bytes（u64 字符串）、helper_mask（整数）。元数据来自已经验证且保活的 C Package，遍历期间不处理其他请求；失败响应没有 programs 字段。响应可能大于请求，客户端应按声明的包/程序容量设置响应预算；传输失败可能留下不完整 JSON，不能视为完整快照。

在同一个仍存活的会话中，客户端可用新序号查询列表，以核对先前加载是否成功。列表提供源码身份和入口信息，不自动判定两个相同源码包的加载是否出于同一次用户意图；重复加载仍会生成不同 ID。重连新进程不会恢复旧列表。

每条响应刷新 stdout。关闭会话后进程继续响应 CLOSED，直到 EOF。写出失败或连接断开可能发生在状态修改之后，调用方不能将没有响应等同于没有执行；当前没有会话恢复，新的进程会话为空。

此版本不监听网络、不提供认证，也不创建 SSH 连接。启动者控制进程及通道访问权限。后续 SSH 前端可复用独立 channel 承载控制流；结构化事件数据应使用单独数据通道，当前尚未实现。
