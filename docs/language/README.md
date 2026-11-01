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

## 普通函数与 hook 入口

普通函数可以独立参考执行，也可由其他函数调用。例如：

```fg
fn increment(pages: u32) -> u32 { return pages + 1; }
```

hook 入口用 `on` 绑定已声明 hook；名称由程序决定，参数签名由模型约束。`use tt_emule.types.{Observation, Core}` 导入模型中的 public 类型，程序不能随意重新定义一个同名 struct 来绕过规范类型检查。

程序文件中的函数以 `program::名字` 选择。模型声明按包和模块组织；源码身份由内容和依赖决定，既不是源文件的绝对路径，也不是 Session 返回的运行时 program_id。

## 从模型文件开始阅读

模型目录包含 `Fingolfin.toml` 和 source 指定的目录，例如 fixture：

```toml
[package]
name = "fixture"
version = "0.1.0"
source = "src"
```

对象声明定义可匹配的结构和逻辑关系：

```fg
pub object fixture { id: u32 }
pub object fixture.device { id: u32 }
pub object fixture.device.core { x: u32, y: u32 }
pub object fixture.device.core.cb { id: u32, pages: u32 }
```

声明不会自动枚举宿主硬件或构造 C 对象。C 模型 adapter 负责实际注册和属性读取；模型中的字段存在也不保证每次采样都能读到值。文件模块、导入和可见性的精确规则见 [模块与类型](modules.md)。

## 参数条件与对象模式

下面的两种条件作用不同：

| 位置 | 数据来源 | 何时执行 |
| --- | --- | --- |
| body 中 `if core.device == 1` | 本次 native frame 的按值调用参数 | 已进入函数体之后 |
| `where tt_emule: { device: { id: 1 } }` | 模型 provider 的对象属性 | 按可用阶段规划，满足可信契约才可提前 |

把 if 写进主体不意味着过滤后的对象不构造 frame；将条件放入 where 也不意味着它一定在挂载时可求值。编译器根据 availability 计划阶段，运行时必须提供对应 facts。tt-emule 身份属性显式声明为 Object；可变状态若没有稳定性保证应保留更晚阶段。

where 的嵌套 record 组合字段条件；例如 fixture 中 `id: 16 as cb_id` 匹配 16 并绑定 cb_id，供主体使用。OR 的匹配可能产生不同绑定行；NOT 不导出绑定。缺少属性产生 unknown，不是 false。完整参考语义见 [模式](patterns.md)，不要用普通二值布尔直觉处理缺失数据。

## 数值、布局与内存

整数采用固定宽度 u/i8、16、32、64。结构体与数组使用检查后的固定布局，禁止按值递归、零长数组和超大布局；指针递归可以表达 ABI，但不授予参考执行器读取宿主内存的能力。enum 的判别值必须能由底层整数表示。

当前目标布局固定为 Linux x86-64 小端。生成 C ABI 带布局断言，可用于核对宿主类型；BPF frame 则有专用 schema 和 encoder。语言类型正确、原生 ABI 一致、BPF frame 正确是三个需分别验证的环节。

## 有界执行与副作用

入口有步数、深度和 VM 指令等预算。预算耗尽是执行错误，不是静默截断统计。函数可通过四个 builtin 修改预配置 Host；没有任意文件、网络或内存写入的通用 builtin。

counter/map/histogram/output 的资源在 C 端配置。语言程序只能请求操作，不能隐式扩容；同一入口前面的副作用不会因后面的错误回滚。函数返回值也不等于 Host 输出，eval 中分别显示 result 和 state。

## 调试顺序

1. `check`：解决符号、类型、hook 和模型契约错误。
2. `eval`：对普通函数给出显式类型的 JSON 参数，查看 result/state/steps。
3. `dispatch`：对 hook 提供对象快照与参数，用参考实现检查匹配结果。
4. `ir`：查看类型化主体和门控阶段，确认优化依据。
5. `build`/`explain`：检查真正交给 C 端的包与入口。
6. `emit-adapter`：生成同源 native frame/encoder，再在目标 C 进程验证。

参考执行并不证明 uBPF、宿主调用点或对象生命周期正确；[C 教程](../c-walkthrough.md) 和真实模型测试补上这些环节。可直接执行的命令见 [快速上手](../quickstart.md)。
