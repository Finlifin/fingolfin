# C 控制程序

`fgctl` 为 C11 可执行程序。支持离线 `inspect` 与通过子进程 stdio 通道持续调用 Agent 的 `session`。挂载、统计订阅和 GDB 联动仍未实现。

无需 uBPF 或 Rust 链接即可构建（需要 json-c、OpenSSL、CMake 和 C 编译器）：

```sh
cmake -S runtime -B /tmp/fg-cli -DFG_WITH_PACKAGE=ON -DBUILD_TESTING=OFF
cmake --build /tmp/fg-cli --target fgctl
/tmp/fg-cli/fgctl inspect path/to/program.fgo
```

Rust 编译器负责事先生成 `.fgo`。成功输出版本为 1 的 JSON，包含程序/模型源码哈希、入口名称、hook、帧大小/对齐、参数/绑定数、返回值标记、helper mask 和 ELF 大小。普通函数的 hook 输出空字符串。所有入口完成包校验后才输出；检查不会执行包内程序。

可追加 `--program-hash SHA256`、`--model-hash SHA256`，要求与包的源码身份一致。未给 pin 时接受任意合法源码身份；内容哈希不提供作者认证。每个选项最多出现一次。

退出码：0 成功，1 包校验/读取/输出/资源失败，2 命令用法错误。诊断写 stderr，失败不输出成功 JSON；传输中断可能使 stdout 留有部分输出，调用方必须检查退出状态。`--help` 显示用法。

测试：启用 `BUILD_TESTING` 和 `FG_WITH_PACKAGE`，配置 `FG_TEST_PACKAGE` 为已生成的包后运行 `ctest --test-dir BUILD -R cli_inspect --output-on-failure`。测试驱动需要 Python 3，产品程序不依赖 Python。

## 持续 Agent 会话（POSIX）

```sh
build/full/fgctl session 42 -- build/full/fg-agent --session-id 42 <<'COMMANDS'
status
load /tmp/device-watch.fgo
list
unload 1
list
close
COMMANDS
```

Agent 的构建还需要启用 uBPF，见 [Agent 文档](../agent/README.md)。示例路径按本机构建结果调整。session ID 由启动者分配且必须与 Agent 一致，每次新会话使用不同 ID。CLI 启动一个新通道进程，按原始 argv 执行 `--` 后命令，不经过隐式 shell；可由调用者显式指定通道程序。当前只实测本地 Agent，SSH 认证、前端会话复用与真实远端集成尚未验收。

stdin 每行一个命令：`status`、`list`、`load PATH`、`unload ID`、`close`。`load ` 后整行是本地路径，允许空格，不进行引号、环境变量或 shell 展开。命令必须以 LF 结束（也接受 CRLF），最多 4095 字节含换行；空行、NUL、未终止行和未知命令失败。LOAD 在本地校验包，然后发送同一份字节及其程序/模型源码 hash pin，避免读取两次路径带来的内容变化。

CLI 初始 sequence=0、generation=1；每条请求递增 sequence，后续请求使用已验证响应中的 generation。stdout 每条请求输出一行原始 JSON 响应，stderr 保留诊断和通道程序的 stderr。Agent 操作错误可继续执行后续命令，但 CLI 最终返回 1；本地命令/读取/传输/响应错误立即停止。所有命令成功且子进程正常退出返回 0，用法错误返回 2。

每次请求的写入与完整响应共享 30 秒期限，响应最多 64 MiB；超限、提前 EOF、错误会话/请求 ID、重复 JSON 键、回退 generation 或坏状态类型会停止会话。列表元数据按 Agent 返回内容透传，客户端不再次验证远端包。子进程收到 stdin EOF 后最多等待 30 秒退出；失败或超时会终止并回收直接子进程。EOF 结束当前 Agent 会话，不保留已加载程序；尚不支持重连恢复、后台持久 Agent 或命令自动重试。

端到端测试为 CTest `cli_session`，覆盖完整 C CLI→C Agent 序列与错误子进程；证据见 [验证记录](../reports/c-cli-session.md)。
