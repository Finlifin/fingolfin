# Fingolfin 项目计划

> 原始设计计划：长期目标与设计依据保留于此。当前使用手册见 [文档目录](docs/README.md)，已实现范围见 [能力说明](docs/development-status.md)。
> 2026-11-01 架构变更：用户要求用户态程序采用 C。本文原有“Rust 负责运行时/控制面”的安排由 [用户态 C 重设计](docs/decisions/c-userspace.md) 覆盖；已确认保留 Rust 编译器与 `.fg` 语言，用户态运行时、控制程序和 SDK 用 C。原有功能与验收目标继续保留。


**版本：** 0.1 · 设计与实施基线
**日期：** 2026-11-01
**目标：** 为硬件 cmodel 提供项目无关的观测语言、编译器和钩子运行时。
**技术约束：** 编译器使用 Rust；解析器使用 pest；项目模型支持分文件、分模块编译；测试仿真器采用 tenstorrent/tt-emule。
**状态：** 本文是实施计划；文中的 Fingolfin API、命令、文件扩展名与生成物为拟定设计，尚未实现。外部项目事实根据文末固定版本的资料核对，本文未实际构建或运行 tt-emule。

---

## 1. 项目定位与成功标准

Fingolfin 将用户编写的观测程序编译成可以挂载到 cmodel 观测点上的钩子。项目模型定义对象、成员 hook、字段和程序属性的具体含义；框架提供通用语言、类型检查、执行计划、环境传递、门控、状态管理和后端。

**基础入口形式：**

```text
fn entry_name on project.object.member_hook(parameters)
where pattern
{
    body
}
```

普通函数使用 `fn helper(parameters) { ... }`。一个文件可以定义多个入口和普通函数，每个入口有独立的 `where`、变量绑定和挂载状态。

### 1.1 首版必须证明的能力

1. 一个文件中的多个入口可以挂载到不同 hook，并可独立启停。
2. 项目模型拆成多个模块后，类型、属性和 hook 解析结果与等价单文件定义一致。
3. 入口签名与 hook 声明一致；C、C++、Rust 和执行后端之间通过 ABI 一致性测试。
4. 上层变量以类型化环境传入；上层稳定条件失败时，可以在进入 VM 前拒绝该入口。
5. 普通字段、计算属性和有限的有状态属性，可以组成新的观测策略。
6. 在 tt-emule 中完成真实 CB、NOC 和执行上下文观测，原有选定测试的功能结果保持一致。
7. CLI 与已有 SSH remote GDB 前端共享同一套会话和观测 API。

### 1.2 首版范围控制

v0.1 的主交付包括：语言子集、模块化模型、C ABI SDK、Rust 编译器、用户态 eBPF 执行、门控、有限状态与输出、CLI、tt-emule 模型包及 GDB 前端连接协议。

后续版本再开展：Linux 内核 eBPF 宿主系统观测、完整逆向调试、模型 checkpoint/replay、LSP、自动策略搜索、任意动态拓扑优化、全量原子事件记录和跨平台 ABI。

核心测试使用一个极小合成模型，覆盖 tt-emule 不具备的语义；tt-emule 是主集成仿真器。合成模型只作为框架测试夹具，不发展为另一套仿真产品。

---

## 2. 语言与语义基线

### 2.1 多入口示例

以下 hook 路径和类型是计划由 Fingolfin 的 tt-emule 模型包定义的接口，不是 upstream 已有接口。示例中的 `emit_cb` 为标准输出接口的封装。

```text
use tt_emule.types.{ObsCall, CbHandle};
use fingolfin.output.emit_cb;

fn emit_pages(call: *ObsCall, cb_id: u32, pages: u32) {
    emit_cb(call, cb_id, pages);
}

fn watch_push on tt.device.core.cb.post_push(
    call: *ObsCall,
    cb: CbHandle,
    pages: u32
)
where tt: {
    device: {
        id: 0,
        core: {
            x: 0,
            y: 0,
            cb: { id: 16 as cb_id }
        }
    }
}
{
    emit_pages(call, cb_id, pages);
}

fn watch_pop on tt.device.core.cb.post_pop(
    call: *ObsCall,
    cb: CbHandle,
    pages: u32
)
where tt: {
    device: {
        id: 0,
        core: {
            x: 0,
            y: 0,
            cb: { id: 16 as cb_id }
        }
    }
}
{
    emit_pages(call, cb_id, pages);
}
```

在此模型包中，`ObsCall*` 是 hook 正式签名的一部分，由运行时提供。已有模型的 hook 若采用其他签名，则生成相应适配入口；框架不会偷偷增加对外参数。

### 2.2 确定的语言规则

| 项目 | v0.1 规则 |
|---|---|
| 入口绑定 | `on` 只能解析到模型包声明的 hook；绑定到具体 hook 种类，命中时有具体对象实例 |
| 参数 | 数量、顺序、类型、传值方式及调用约定都与 hook 声明校验 |
| 返回值 | 观测入口首先限制为 `void`；普通函数支持固定布局返回值 |
| `where` | 入口自己的匹配条件；省略时视为真；普通函数暂不复用该语法表达泛型或契约 |
| 对象锚点 | 模式中与 hook owner 对应的对象就是本次命中的实例 |
| 匹配次数 | 单值关系产生零或一组匹配；模型声明的多值关系可产生多组；每组调用一次入口主体 |
| 变量绑定 | 只在该入口成功匹配后的主体内可见；不同入口互不污染 |
| `or` | 各分支必须导出相同名字和兼容类型；否定模式中的局部绑定不导出 |
| 范围 | `a..b` 为半开区间，`a..=b` 为闭区间；溢出与边界有明确诊断 |
| 副作用 | `where` 禁止可见写入和输出；属性读取仍携带采样时刻、依赖和有效期 |
| 未知数据 | 缺失必要能力在编译/挂载时诊断；动态初始化中的未知值单独统计覆盖状态 |
| 函数调用 | helper 通过显式参数获得匹配值；调用 helper 不重新触发 `where` |
| 示例中的 `...` | 仅为讨论时的省略号；首版不支持 C varargs |

**单次 hook 命中仍然是一次 ABI 入口调用。** 多组模式绑定由生成的入口内部枚举并执行主体，不改变 hook 参数表。

### 2.3 C ABI 子集

支持固定宽度整数、明确表示的布尔值、定长数组、C 布局 struct、固定底层整数宽度的无载荷 enum、受限指针/不透明 handle、普通函数。首版只承诺 Linux x86-64 小端目标，其他目标单独建立 ABI profile。

生成 C 头文件、Rust `repr(C)`/明确整数表示、布局摘要，以及 `sizeof/alignof/offsetof` 和跨语言调用测试。固定 enum 的 wire representation，不依赖编译器默认 C enum 宽度；输入时验证判别值。模型内部 C++ 对象通过薄 C wrapper、局部只读视图或 handle 接入，不直接暴露 STL 和 C++ 类布局。

布局一致和函数调用 ABI 一致是两个不同验收项。Rust Reference 明确区分这两件事，并说明了 C 表示及 enum 表示的边界。[S9]

### 2.4 从 Luna 借鉴的范围

借鉴 `fn`、`struct`、`enum`、点路径、`use`、`pub/private`、`let/var`、`--` 注释、`.variant`、模式组合及属性写法。模块组织借鉴文件/目录到模块的映射和再导出机制。[S1][S2]

`fn ... on ... where pattern`、项目模型 hook 描述、观测环境和门控属于 Fingolfin 自身扩展。尤其是 Luna 的 `where` 已用于它自己的子句体系，本项目只借鉴表面风格，单独定义观测含义。

首版采用普通的 `^attrs.abi(.c)` 风格标注或默认 C ABI 配置，不引入 Luna 的完整 comptime、trait、effect、引用责任系统和泛型特化系统。新增语法进入 `docs/language/provenance.md`，记录来源版本、借用范围和差异。复制源码前完成许可证与溯源审查。

---

## 3. 模块化项目模型

模型是项目特定的。框架不内置 GPU、CTA、warp、Tensix 或 CB 知识。

### 3.1 建议模型包布局

```text
models/tt-emule/
├── Fingolfin.toml
├── Fingolfin.lock
├── src/
│   ├── lib.fg                  # 小型公共入口，只负责导出
│   ├── types/
│   │   ├── mod.fg              # 统一再导出类型
│   │   ├── handles.fg
│   │   ├── events.fg
│   │   └── enums.fg
│   ├── objects/
│   │   ├── device.fg
│   │   ├── core.fg
│   │   ├── kernel.fg
│   │   ├── cb.fg
│   │   └── noc.fg
│   ├── hooks/
│   │   ├── lifecycle.fg
│   │   ├── cb.fg
│   │   └── noc.fg
│   └── properties/
│       ├── identity.fg
│       ├── cb_state.fg
│       └── execution.fg
├── bindings/
│   ├── lifecycle.toml
│   ├── cb.toml
│   └── noc.toml
├── native/
│   ├── adapter.h
│   └── adapter.cpp
└── tests/
    ├── model_check/
    ├── abi/
    └── scenarios/
```

`.fg` 和清单名是本计划建议的命名。模型表达式、类型与程序复用一个语言前端；TOML 只保存包信息和物理映射元数据，不另建第二套可执行脚本语言。

### 3.2 模块语义

借用 Luna 的文件模块、目录模块和 `pub use` 思路：扫描清单选定的源码根，普通文件成为模块，`lib.fg` 为包根，`mod.fg` 可作为目录主体。显式管理生成源码；同级同名文件/目录冲突报错。[S2]

**文件模块树、对象关系图、hook 路径是三种不同结构。**

例如，`hooks/cb.fg` 可以定义并导出逻辑路径 `tt.device.core.cb.post_push`；无需把文件也放进相同的多级目录。对象关系属于项目 schema，不能仅由文件布局推断。

采用声明收集与主体检查两阶段编译。不同模块通过稳定符号引用共享对象类型，不拷贝类型定义；包内成员扩展具有唯一 owner，重复定义报错。类型引用允许通过声明阶段解析；按值递归布局、循环初始化和属性维护依赖环需诊断。

程序依赖包与模型依赖包都锁版本。模型 schema、目标 ABI、hook signature 和物理绑定形成独立 fingerprint；构建产物绑定这些 fingerprint 及 simulator build ID。

### 3.3 模型必须提供的元信息

| 声明 | 必要内容 |
|---|---|
| 对象 | 类型、实例身份、生命周期、父子/关联关系、关系基数 |
| Hook | 规范路径、owner 类型、正式 C ABI 参数、事件阶段、参数有效期 |
| 字段/属性 | 类型、读取实现、采样时刻、读写效果、稳定范围 |
| 缓存能力 | 可缓存范围、更新通知、失效规则、初始化方法 |
| 状态维护 | 输入 hook、状态键、容量、执行顺序、清理时机 |
| 物理绑定 | 观测点标识、参数投影、局部实例定位及版本要求 |

这些信息由项目模型包提供。Cmodel 观测点继续传入本地已有参数；上层字段的查询和投影由生成的代码及 adapter 实现。

### 3.4 模块化验收

在相同合成模型规模下比较单文件与 30 个模块的版本：可观察结果一致；改变一个属性模块只使依赖模块失效；扫描顺序不改变符号身份；新增文件不会自动将其中所有入口激活。发现源码、导出符号和运行时挂载是三个独立步骤。

---

## 4. 总体架构与 Rust/C 工程

```text
.fg 观测程序 + 项目模型包
            ↓
   pest → AST → 名字解析/类型检查
            ↓
      typed HIR / Pattern IR
            ↓
   取数依赖、环境布局、门控分期
            ↓
       Observation Plan IR
            ↓
    bounded MIR / ABI lowering
            ↓
  受限 C 后端 → Clang → eBPF ELF
            ↓
  C ABI adapter → 用户态 eBPF VM
            ↓
   结构化事件、聚合、控制请求
            ↓
      Agent / CLI / GDB 前端
```

Rust 负责编译器和离线计划生成；C11 负责用户态运行时、计划执行、容器/状态、Agent、控制 CLI 和 SDK。模型为 C++ 时使用薄 C wrapper；产品运行时不链接 Rust Host 或回调 Rust 执行器。eBPF VM 优先评估已有实现，避免同期开发新的 JIT。uBPF 提供解释器和 x86-64/Arm64 JIT，可作为首个原型候选；是否最终采用，由 M0 兼容性、安全与性能验证决定。[S11]

### 4.1 编译器 workspace 与独立 C 用户态工程

```text
crates/
├── fg-source/           # 源文件、span、诊断、模块发现
├── fg-syntax/           # pest grammar、AST、语法测试
├── fg-model/            # 项目 schema、符号、绑定、ABI 描述
├── fg-sema/             # 名字解析、类型/效果/生命周期检查
├── fg-ir/               # HIR、Pattern IR、Plan IR、bounded MIR
├── fg-plan/             # 依赖展开、门控、环境、状态共享
├── fg-codegen/          # 受限 C、Clang 调用、BPF 产物、source map
└── fg-cli/              # 编译侧 check/build/explain 等
runtime/                # C11，独立 CMake/CTest
├── include/fingolfin/   # 公共 C ABI、版本化契约
├── src/                # loader、VM、gate、路由、状态、输出
└── tests/              # 纯 C 测试与 sanitizer 验证
agent/                  # C11 会话、控制协议、远端传输
cli/                    # C11 attach/detach/configure/统计等控制命令
sdk/
├── include/
└── tests/
models/
├── fixture/             # 核心语义测试用极小模型
└── tt-emule/
examples/
tests/
docs/
```

上述为目标目录划分；现有单 crate 和 Rust runtime 是迁移前实现。初期可以合并编译器 crate，保持逻辑依赖边界即可。C 用户态工程须能够在没有 Rust 工具链和运行时库的环境中，使用预生成产物独立构建/测试。`fg-*` 核心 crate 不得依赖 tt-emule 的类型、源码目录或对象名称。

### 4.2 pest 实施要求

使用 pest 解析定义、语句和模式；表达式运算优先级使用 `PrattParser`。pest 的 PEG 是有序选择，长运算符和具体产生式的顺序会影响解析，必须测试 `..`/`..=`、关键字边界和 pattern/body 的大括号边界。[S7][S8]

建议按 `lexical.pest`、`items.pest`、`expressions.pest`、`patterns.pest` 分拆维护；构建时确定性合并 grammar，保留片段映射和规则重名检查，不以文本 include 代替模型模块系统。

产生式草案：

```text
hook_fn -> fn id on hook_path ( parameter_list? ) where_clause? block
ordinary_fn -> fn id ( parameter_list? ) return_type? block
where_clause -> where anchored_pattern
```

所有 AST 节点保留 span。首版保证单文件首个语法错误可定位，以及解析成功后跨文件批量语义诊断；多错误语法恢复作为后续增强，避免在 M1 同时重写解析框架。

---

## 5. C ABI 与 eBPF 后端边界

**对外 C ABI 和内部 BPF 调用约定分别定义。** BPF 的推荐调用约定使用 R1–R5 传参，不能直接将任意宿主 C 函数原型当作 BPF 入口。[S10]

每种 hook 生成签名一致的 native adapter：

```text
Cmodel 观测点：局部实参
    ↓
实例路由/门控
    ↓
签名匹配的 C ABI adapter
    ↓
校验并构造该 hook 的定长调用 frame
    ↓
eBPF entry(frame)
    ↓
剩余 where + 用户主体
```

调用 frame 使用固定布局、边界明确的参数投影。小型标量直接复制；大型借用对象通过经验证的视图或 handle 读取，避免每次复制完整对象。原始 C struct by-value 和 pointer 参数分别有 ABI 测试，不能在 lowering 中静默互换。

首版使用 Rust 编译器生成受限 C，再由 Clang 编译 BPF；这条路线须在 M0 验证结构体、函数调用、重定位、helper、stack 和 source map。保留解释执行器作为语义参考；同一 MIR 可生成 native C 测试后端用于差分，避免同时建设 LLVM 直出后端。

### 5.1 执行和安全范围

首版支持固定上界循环、有限函数调用、有界 map/counter/histogram、结构化输出和预注册只读模型 accessor。限制递归、任意堆分配、任意宿主函数调用和裸模型内存写入。

输出与控制通过 allowlist helper。每个程序声明状态内存、输出量与执行预算；预算超限产生诊断和可配置停用策略。VM 的解释器/JIT 一致性、越界检查、helper 指针验证和终止性单独测试。编译器约束与 VM 验证各承担明确责任。

v0.1 面向可信开发者，不宣称对任意恶意脚本提供进程隔离。模型 adapter 和 native 代码处于可信计算基中；未来开放给不可信用户时另做威胁建模和隔离工程。

---

## 6. 环境传递与分层门控

### 6.1 三种环境

| 环境 | 内容 | 生命周期 |
|---|---|---|
| AttachmentEnv | 用户配置、阈值、标签、输出目的地 | 一次挂载/配置代际 |
| ScopeBindings | 上层模式产生的稳定字段和对象身份 | 对象实例及其有效版本 |
| InvocationLocals | hook 参数、瞬时属性、多值模式绑定 | 本次调用/本组匹配 |

入口只捕获实际使用字段，按需执行常量传播、字段平铺和只读块共享。普通函数通过显式参数传值。大型可变共享对象使用明确的状态接口，不把栈变量地址留给未来 hook。

需要环境参数时，模型 hook 正式声明 `ObsCall*`；兼容已有签名时由生成的 thunk 适配。用户配置与对象绑定的物理存储不依赖 TLS 中“当前程序”全局变量。

### 6.2 门控分期

```text
where
  ├── 挂载期：只依赖配置
  ├── 对象期：生命周期内稳定的字段
  ├── 更新期：具备完整失效通知的属性
  └── 命中期：当前指令或局部状态
```

设完整 predicate 为 P，提前门控为 G，优化必须保证 `P ⇒ G`。无法证明有效性的条件留在命中期。缓存失效期间采用保守通过并实时求值，或者明确进入未就绪状态；不让旧 false 门控漏掉新匹配。

快路径目标：

```text
本地实例端口 → 获取当前路由 → 无订阅者则返回
                                  ↓
                        建 frame → VM → 剩余 where → fn
```

关闭路径的观测代码不构造高成本 frame、不进入 VM、不做输出、不执行项目专用上层 accessor。观测点参数若本来就需昂贵计算，SDK 使用 guard-before-evaluation 形式延迟计算；测试禁止宏重复求值。

多程序共享观测点时，每个程序有独立资格；整体路由仅在所有程序均无资格时关闭。状态维护程序保留自己的调度条件，确保 false 条件变 true 后能重新开启。

### 6.3 并发、失效与热切换

配置、环境、路由作为一致 generation 发布。运行时采用 epoch/安全点等机制等待在途调用退出后释放旧代码和数据；M0 原型评估选型，不在热路径默认引入全局 mutex。

对象身份包含运行/launch epoch 与实例 generation，避免地址、slot、CB 编号复用造成串线。支持配置更新、对象销毁、重新绑定、入口卸载和模型结束的测试。

若项目无法提供可靠实例句柄，则模型 adapter 使用旁表查找并在 `explain` 标明代价。框架不承诺仅凭 hook 路径即可做到实例级常数开销门控。

---

## 7. tt-emule 集成计划

### 7.1 固定研究与构建基线

本计划核对的版本：

```text
luna:
  44bf45ec66c8b249a4eb3e016e965b119a60ed2f

tt-emule:
  07f1bd8301544403c8bc1faa4038f6cbf69909f1

tt-metal（来自该 tt-emule 的 tt-metal-pin.txt）:
  d48d09dee19de51f694a52fdf75d569950d38ceb
```

实施时把三者写入锁文件。升级通过独立兼容性变更，运行同一组基线测试。[S1][S3][S4]

### 7.2 适用范围

tt-emule 是 tt-metal 设备侧 kernel 与 host API 的 C++ 软件仿真层，包含 CB/DFB、DST、NOC 和同步原语。它集成到 tt-metal 构建，而非 standalone 构建。[S3]

它不提供真实 RISC-V ISA 或周期精确时序；README 描述的 NOC 操作为同步 memcpy。因此首版验证真实的 API、缓冲与逻辑执行上下文观测，GPU `post_decode` 示例保留在合成模型测试。NOC、wait 的耗时标注为宿主仿真耗时，不能解释为硬件 DMA 延迟或 GPU stall cycle。[S3]

README 的线程描述不是身份设计的唯一依据：固定版本源码已有 `__emule_self`、每逻辑执行上下文数据以及 fiber park/wake 路径，部分 sanitizer 状态明确为避免跨 fiber 污染而存放在 per-fiber context。[S5][S6]

### 7.3 拟接入的 hook

| 模型 hook（拟定） | 候选实现区域 | 首版证据 |
|---|---|---|
| `tt.device.program.launch_begin/end` | host API / program runner | launch 身份、配置、结束清理 |
| `tt.device.core.kernel.enter/leave` | runner 与逻辑执行上下文初始化/退出 | core、角色、执行实例身份 |
| `tt.device.core.cb.post_push/post_pop` | `include/jit_hw/api/cb_api.h` | CB ID、页数、局部上下文 |
| `tt.device.core.cb.pre_wait/post_wait` | CB wait/park 边界 | 一次等待区间及原因 |
| `tt.device.core.noc.pre_read/post_read` | dataflow NOC API | 本地/远端地址、字节数、执行上下文 |
| `tt.device.core.noc.pre_write/post_write` | dataflow NOC API | 写入范围、字节数、调用位置 |

此表是插桩候选清单。M0 结合源代码确定每个位置的实际阶段、所有早退路径和已持有局部参数。尤其 CB fast path、self-recycle 分支及 fiber 等待路径都要覆盖。[S5]

观测点只报告局部事实；adapter 建立对象与生命周期关系。若事件需要精确的更新后状态，必须在对应更新边界取得一致快照；在锁外临时读取到的值标为 sampled value，不冒充该操作的精确 post-state。

### 7.4 JIT 与共享运行时

JIT 编译的 kernel `.so` 需要访问同一进程内的 Fingolfin runtime。采用单一 runtime 实例和显式初始化/函数表绑定，验证 DSO 之间共享注册表，避免每个 `.so` 各自拥有一份状态。

Fingolfin SDK、模型 ABI 和相关 adapter 变化必须进入 kernel JIT 缓存键；源码路径与 `dlopen/dlclose` 生命周期进入调试与卸载测试。观测程序热换通常只更新 runtime 路由，新增/修改观测点才触发 kernel 重编译。

第一阶段固定 Wormhole 与精选 C++ 用例；Blackhole、Quasar/DFB 扩展在核心链路稳定后进行。完整 tt-mlir/D2M 环境不作为第一个 demo 的依赖。

### 7.5 三个验收场景

**场景 A：选择性 CB 统计。** 在指定 device/core/CB 上累计 push/pop 页数，其他对象在 VM 前被门控；同一文件含多个入口，计数分别可解释。

**场景 B：NOC 访问窗口。** 指定逻辑执行上下文和地址范围，在线聚合读写字节数并保留有界证据；参数更新后在明确 generation 生效。

**场景 C：等待后动态加细节。** 平时仅维护等待摘要。触发条件成立后，控制面激活该对象上的另一组入口，并明确标出新策略生效边界。激活后的细节仅覆盖未来事件，已有历史取决于先前配置的有界缓存。

---

## 8. GDB 与现有 SSH 前端

复用现有 SSH 通道与 GDB 会话，不另建调试器前端。GDB/MI 本身面向将 GDB 作为大型系统组件的集成场景。[S12]

首版提供：

- 模型/hook 浏览、签名查询、源码编译及诊断。
- 单入口和整包 attach/detach、配置更新、计划解释、预算与数据缺口显示。
- 事件关联到 run/launch/对象/入口/源码位置。
- 请求现有 GDB 会话暂停、设置宿主断点或查看已有证据；暂停由会话控制器统一仲裁。

控制协议采用带版本的 JSON-RPC 风格请求，数据面单独传输结构化记录，复用 SSH 的独立 channel。事件历史不自动等于执行 replay。`snapshot/replay/goto-event` 作为能力接口预留，只有模型及调试后端声明支持时才启用。

首版的 GDB 停点与最初观测事件分别显示；不能用当前宿主栈冒充历史事件现场。

---

## 9. CLI 与交付物

拟定命令：

```bash
fingolfin model check models/tt-emule
fingolfin check examples/tt-emule/cb_watch.fg --model models/tt-emule
fingolfin build examples/tt-emule/cb_watch.fg --backend ubpf
fingolfin explain cb_watch.fgo --entry watch_push
fingolfin attach cb_watch.fgo --session SESSION --config config.json
fingolfin update ATTACHMENT --config config.json
fingolfin detach ATTACHMENT
```

这里的 `ubpf` 是候选后端名称，最终由 M0 决策确定。

`.fgo` 程序包包括：版本清单、各入口代码与依赖、hook signature hash、配置/绑定布局、状态定义、门控计划、输出 schema、source map、模型 fingerprint、资源预算和 helper 需求。一个入口卸载不会卸载仍被其他入口引用的依赖。

---

## 10. 测试与质量门禁

| 层次 | 核心用例 | 合入标准 |
|---|---|---|
| Parser | 多入口、where/body 边界、嵌套模式、range/or/as、注释、关键字边界 | 正反例 golden tests；所有错误有 span |
| 模块 | 导入、再导出、重复声明、私有符号、目录变化、依赖升级 | 模块化与单文件语义等价；增量失效正确 |
| 类型/ABI | 错签名、struct 嵌套、enum 宽度、by-value、指针、输出布局 | C/C++/Rust 双向调用及布局一致 |
| 计划器 | 属性依赖、缓存合法性、条件重排、多值匹配、unknown | 参考解释器与优化实现输出等价 |
| eBPF | helper、栈界、状态界、bounded loop、异常、解释/JIT | 同一 corpus 下结果一致、非法访问拒绝 |
| 运行时 | 多实例、共享依赖、热更新、卸载、重入、资源溢出 | 不串线、无 UAF、状态清理完整 |
| tt-emule | CB/NOC/等待、多核、多角色、fiber 切换、DSO | 选定上游功能测试与基线一致 |
| Debug | SSH 断连、重复命令、旧 generation、GDB 异步停止 | 会话状态清楚、请求幂等或显式失败 |

使用属性测试比较朴素 predicate evaluator 与优化 gate+residual 计划；重点生成配置修改、对象复用、状态从 false 变 true、多个入口交错等序列。乱序传播不能产生假阴性。

压力测试至少覆盖 10,000 次配置/路由切换，并在可用环境运行 ASan/UBSan、Rust 单元测试和适用的并发检测。检测器兼容性与 fiber 运行方式另建配置，避免把工具自身限制当成框架通过。

### 10.1 性能测量协议

在固定主机、构建选项、输入和 JIT cache 状态下分别测量：

1. 原始仿真器基线；
2. 编译入 SDK，但未挂载；
3. 已挂载，静态上层条件全失败；
4. 门控通过、剩余条件失败；
5. 仅计数/直方图；
6. 输出匹配证据；
7. 1、8、32 个入口及不同对象命中比例。

记录端到端 slowdown、hook 调用成本分布、VM 进入次数、frame 构造次数、内存峰值、事件量和丢弃计数。冷 JIT 构建和热执行分开报告。

**初始性能预算（待 M0 基线校准，并非测量结果）：**

- 在约定代表性计算用例上，未挂载或静态全拒绝路径以 slowdown ≤ 3% 为预算目标。
- 限定对象的计数/直方图用例以 slowdown ≤ 10% 为预算目标。
- “静态全拒绝”还必须满足 VM 进入次数、昂贵 frame 构造次数和输出次数为 0。
- 热路径无逐次堆分配、无同步打印、无无界容器增长。
- 详细输出按预算运行，数据丢失可检测；要求完整证据的分析报告 incomplete。

hook 密集型微基准单独报告，不能用少数低频用例掩盖分派成本，也不为所有 workload 承诺统一比例。

---

## 11. 实施阶段与人员假设

**排期假设：4 名全职工程师，已有 GDB 前端维护者提供接口评审支持。基线 18 周，另预留最多 4 周风险缓冲。** 这些是项目排程估算，不代表已经启动的工作或交付承诺。每阶段以验收门禁驱动。

| 阶段 | 周次 | 主要交付 | 退出条件 |
|---|---|---|---|
| M0 技术验证与契约 | 1–2 | 语言/ABI/model RFC；固定 tt-emule+tt-metal 环境；C→VM 原型；性能基线 | 一个真实 CB 观测点可触发 C ABI→VM 路径；明确 eBPF 后端选型；摸清 JIT DSO 与 fiber 上下文 |
| M1 编译前端与模块 | 3–5 | pest、AST、名字/类型检查、模型包、普通函数与多入口、check/explain | 单文件和模块化模型等价；错误签名和未知属性诊断通过；GPU 示例在 fixture 上可解析检查 |
| M2 首个端到端闭环 | 6–8 | ABI SDK、参考 evaluator、最小 eBPF lowering、CB adapter、结构化输出 | 从 .fg 编译到 tt-emule 运行，两个入口均正确；C/C++/Rust ABI 测试通过 |
| M3 环境与高效门控 | 9–12 | 类型化配置、scope binding、VM 前 gate、状态维护、更新/卸载 | false→true 无漏匹配；多实例/fiber 不串线；达到校准后的性能门禁 |
| M4 策略组合与真实场景 | 13–15 | CB/NOC/等待三个包；多入口共享依赖；Agent/CLI；动态策略激活 | 三个场景完整验收；预算、数据缺口、配置 generation 可解释 |
| M5 调试集成与发布 | 16–18 | SSH 前端插件协议、GDB 联动、回归/性能报告、开发文档 | release checklist 通过；全新环境按文档构建并复现示例 |

### 11.1 工作分工

- A：语言、pest、语义与模块系统；兼任核心语言 RFC owner。
- B：IR lowering、eBPF/VM、ABI adapter、门控与生命周期。
- C：tt-emule 模型包、C++ SDK、构建/JIT 接入和场景测试。
- D：质量工程、Agent/CLI、GDB 协议与性能测量；与 A/B/C 共同维护测试。

跨模块任务由负责实现的一方提交 RFC，至少另一条工作线 review。同一人承担多个角色时，应依据关键路径重新排程，不能将人数等比例缩减而保持工期。

### 11.2 关键路径

```text
Hook ABI / 模型契约
 → 多模块解析与类型检查
 → C ABI→VM 调用链
 → 配置环境与门控
 → tt-emule 场景
 → 前端与发布
```

GDB UI 设计、文档和 CI 镜像可并行推进。M0 遇到 VM 或 ABI 不兼容时先调整后端设计，避免把核心风险拖到 M3。

### 11.3 阶段验收与证据归档

每个阶段提交 `reports/M<N>/` 验收包。包内包含源码 commit、依赖锁文件摘要、主机与工具链版本、构建选项、执行命令、退出码、原始日志及结论；性能报告另附重复次数、原始样本和统计方法。尚未执行的检查标为 `not-run`，受环境限制的检查标为 `blocked` 并记录原因，不能以设计说明代替运行证据。

| 阶段 | 最小可审阅证据 | 未通过时的处理 |
|---|---|---|
| M0 | 真实 CB→adapter→VM 调用日志；Clang/VM 能力矩阵；原始性能基线；DSO/fiber 身份实验 | 记录后端或 ABI 决策变更，重跑受影响实验后再冻结契约 |
| M1 | parser 正反例；单文件/30 模块等价报告；签名与可见性诊断样例 | 修正语言或模块契约并补回归用例 |
| M2 | 两个入口的可复现输出；C/C++/Rust 布局和双向调用报告；真实 CB 集成日志 | 分离 ABI、lowering 与插桩失败，逐层复现 |
| M3 | false→true、对象复用及至少 10,000 次切换结果；零 VM/frame 拒绝路径计数；性能报告 | 不发布未经证明的缓存优化；修复后重跑语义与性能门禁 |
| M4 | 三个场景的输入、期望值、实际结果；预算溢出和数据缺口记录；动态激活 generation | 将缺失能力作为未完成项，不以合成模型结果代替真实集成 |
| M5 | 第 14 节逐项证据索引；干净环境重建日志；前端/GDB 联动记录 | 保持 release candidate 状态，关闭全部发布阻塞项后发布 |

技术决策记录存入 `docs/decisions/`，每条包含问题、候选方案、实测证据、选择及代价、受影响契约和重新评估条件。M0 至少需要决定 VM 后端、frame ABI、并发回收机制与 tt-emule 插桩边界。计划中的候选方案只有在对应决策记录通过评审后才成为实施基线。

阶段 owner 汇总证据，由另一条工作线复核。性能预算调整须保留原目标、实测差距、适用 workload 和调整理由；不能仅因实现未达标而静默放宽。任何范围变更同步修改工作包、阶段退出条件及发布清单，保留变更记录。

---

## 12. 可转成 issue 的工作包

| ID | 工作包 | 交付物 | 依赖 | 主要阶段 |
|---|---|---|---|---|
| FNG-001 | 语言与多入口语义 RFC | 语法、绑定、cardinality、错误规则 | 无 | M0 |
| FNG-002 | Hook/C ABI/frame RFC | SDK ABI、布局检查、签名 hash | 001 | M0 |
| FNG-003 | 模型模块与绑定 RFC | 包规则、对象/模块分离、依赖声明 | 001 | M0 |
| FNG-004 | tt-emule 固定构建环境 | lock、镜像配方、基线日志 | 无 | M0 |
| FNG-005 | 用户态 eBPF spike | VM/Clang/helper/ABI 测试报告 | 002 | M0 |
| FNG-006 | pest 前端 | grammar、AST、span、golden tests | 001 | M1 |
| FNG-007 | 多文件模块发现/解析 | 符号索引、可见性、再导出 | 003,006 | M1 |
| FNG-008 | 类型与模式检查 | hook 签名、属性类型、or/as/range | 006,007 | M1 |
| FNG-009 | 模型 schema 与元数据 | 属性有效期、物理绑定校验 | 003,008 | M1 |
| FNG-010 | 参考 evaluator 与 Plan IR | 未优化语义执行、explain | 008,009 | M1–M2 |
| FNG-011 | SDK 与 native adapter | 观测点宏、C ABI wrapper | 002,004 | M2 |
| FNG-012 | MIR/C/BPF lowering | ELF、source map、限制检查 | 005,010 | M2 |
| FNG-013 | runtime loader/dispatch | 加载、helper、frame、输出 | 005,011,012 | M2 |
| FNG-014 | tt-emule CB adapter | 多入口样例、共享 runtime 接入 | 004,011,013 | M2 |
| FNG-015 | 配置与作用域环境 | typed env、布局裁剪、generation | 009,013 | M3 |
| FNG-016 | predicate 分期/门控 | gate+residual、保守退化 | 010,015 | M3 |
| FNG-017 | 生命周期与并发安全 | 对象复用、fiber、热更新/卸载 | 013,015,016 | M3 |
| FNG-018 | 有界状态与属性维护 | counter/map/histogram、初始化 | 009,013 | M3 |
| FNG-019 | NOC/等待 adapter | 两类真实场景与语义说明 | 014,017,018 | M4 |
| FNG-020 | 多程序组合与依赖共享 | 独立启停、共享引用、触发更新 | 016,017,018 | M4 |
| FNG-021 | Agent/CLI 控制 API | 会话、幂等、版本、统计 | 013,015 | M4 |
| FNG-022 | SSH/GDB 前端扩展 | 浏览/编译/挂载/事件联动 | 020,021 | M5 |
| FNG-023 | ABI/差分/并发测试工程 | 持续回归与故障注入 | 006 起持续 | M1–M5 |
| FNG-024 | 性能与发布工程 | 基线、预算、release checklist | 004 起持续 | M0–M5 |

---

## 13. 主要风险与处置

| 风险 | 表现 | 应对 |
|---|---|---|
| C ABI 与 BPF 混淆 | 大结构、参数寄存器、指针传递错误 | 两套契约、生成 adapter、跨语言实测 |
| 缓存 predicate 过期 | false 后漏掉新匹配 | 完整失效依赖；缺少通知时实时检查；unknown 可见 |
| fiber/多实例上下文串线 | 错对象、错配置、状态污染 | 显式逻辑实例身份与 generation；不用宿主线程 ID 代替对象 |
| JIT DSO/runtime 分裂 | 某些 kernel 无法看到挂载 | 单一 runtime，显式绑定，缓存键与卸载测试 |
| 模型文件再次膨胀 | 修改影响面大，入口文件变成业务集合 | 按子系统拆分；lib 仅导出；增量与模块等价验收 |
| adapter 上游漂移 | pin 更新后访问规则失效 | 版本锁、fingerprint、兼容测试和独立升级 PR |
| 过量细节输出 | 运行慢、内存增长、证据缺口 | 在线聚合、有界窗口、按对象门控、明确 overflow 语义 |
| 仿真结果被误读 | 将宿主耗时当硬件时序 | 能力声明、时间域标签、tt-emule 限制写入报告 |
| 语言范围失控 | 同期实现通用语言和完整 verifier | 固定子集、有限属性系统、已有 VM、阶段 gate |
| 观测改变并发行为 | 原问题消失或新问题出现 | 保留低开销模式、A/B 功能回归、报告调度扰动限制 |

---

## 14. v0.1 发布验收清单

发布候选必须同时满足：

- 新语法、多入口文件、模块化项目模型和严格签名检查可用。
- 整个包与单入口的安装、参数更新、启停、卸载均有命令与测试。
- 稳定上层条件拒绝时，计数器证明 VM 与昂贵参数构造均未发生。
- predicate 未优化/优化版本、参考执行器/eBPF 执行结果在测试 corpus 中等价。
- 三个 tt-emule 场景及固定上游回归子集通过；上游既有失败与新增失败分开归档。
- 热更新、fiber 切换、对象地址复用和 DSO 生命周期测试通过。
- 预算溢出、事件丢失、属性未就绪和不支持能力均可见。
- 前端可浏览并管理观测，事件与当前 GDB 上下文有明确区分。
- 文档包含语言参考、模型作者指南、SDK 接入、性能方法和完整重建步骤。

**第一轮评审只冻结三份契约：语言与匹配语义、Hook/环境 ABI、模块化模型与属性依赖。随后以“同一文件两个 CB hook + 实例级门控 + 类型化配置”为第一条端到端交付路径。**

---

## 资料与版本依据

以下地址用于独立审阅；GitHub 引用尽可能固定到本次核对的 commit。官方文档按 2026-11-01 查阅记录，实施时锁定工具版本。

- **[S1] Luna 语法定义**：https://github.com/Finlifin/luna/blob/44bf45ec66c8b249a4eb3e016e965b119a60ed2f/docs/grammar/syntax
- **[S2] Luna 模块与源码组织**：https://github.com/Finlifin/luna/blob/44bf45ec66c8b249a4eb3e016e965b119a60ed2f/docs/code-organization/modules-and-sources.md
- **[S3] tt-emule README、能力与构建约束**：https://github.com/tenstorrent/tt-emule/blob/07f1bd8301544403c8bc1faa4038f6cbf69909f1/README.md
- **[S4] tt-metal pin**：https://github.com/tenstorrent/tt-emule/blob/07f1bd8301544403c8bc1faa4038f6cbf69909f1/tt-metal-pin.txt
- **[S5] tt-emule CB 实现与 fiber 等待路径**：https://github.com/tenstorrent/tt-emule/blob/07f1bd8301544403c8bc1faa4038f6cbf69909f1/include/jit_hw/api/cb_api.h
- **[S6] tt-emule 逻辑执行上下文**：https://github.com/tenstorrent/tt-emule/blob/07f1bd8301544403c8bc1faa4038f6cbf69909f1/include/jit_hw/internal/emule_thread_ctx.h
- **[S7] pest PEG 与 ordered choice**：https://pest.rs/book/grammars/peg.html
- **[S8] pest PrattParser**：https://pest.rs/book/precedence.html
- **[S9] Rust 类型布局与 C 表示**：https://doc.rust-lang.org/reference/type-layout.html
- **[S10] BPF ABI 推荐约定**：https://docs.kernel.org/bpf/standardization/abi.html
- **[S11] uBPF 官方文档**：https://iovisor.github.io/ubpf/
- **[S12] GDB/MI 官方接口**：https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI.html
