# `.fg` 语言与模型

模型包声明对象、hook 和参数类型；观测程序声明入口和计算。模型用 Rust 编译器检查，生成代码由 C 运行时执行。

```fg
use tt_emule.types.{Observation, Core};

fn watch on tt_emule.device.core.cb.cb_push_back(
    call: Observation, core: Core, cb_id: u32, n: u32
) {
    if core.device == 1 && cb_id == 0 && call.phase == 0 {
        builtin.counter_add(0, 1);
        builtin.emit(cb_id, core.device);
    }
}
```

counter 0 与输出容量需要由 Host 预先配置。Core.device 来自实际设备，phase 0 表示 API 进入，phase 1 表示退出，不能把退出当作成功结果。

## 编译命令

| 命令 | 用途 |
| --- | --- |
| parse / inspect | 语法与模型发现；不激活入口 |
| inspect --types / --bodies | 类型/契约检查，或进一步检查函数体 |
| check | 独立检查模型后检查观测程序 |
| eval / dispatch | Rust 参考执行普通函数或 hook |
| ir | 查看类型 IR 与阶段 gate |
| abi | 生成模型 C/C++ 或 Rust ABI 声明 |
| emit-c / emit-bpf / emit-adapter | 生成后端源码与 C adapter |
| build / explain | 生成或校验开发 `.fgo` |

具体参数以 `cargo run -- COMMAND --help` 为准。运行示例见 [快速上手](../quickstart.md)。

## 阅读路径

- [模块与类型](modules.md)：源文件组织、可见性和固定布局。
- [where 模式](patterns.md)：嵌套字段、范围、绑定、or/not 与 unknown。
- [属性阶段](../model-availability.md)：可信模型契约与提前门控。
- [状态与 helper](../runtime-containers.md)：counter、输出、map、histogram。
- [语法来源](provenance.md)：设计溯源与当前子集。

当前没有浮点、泛型、trait、裸内存写入或无界循环。指针作为显式 ABI 值传递，语言参考执行器不解引用目标地址。
