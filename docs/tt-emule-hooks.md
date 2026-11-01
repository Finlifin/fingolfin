# tt-emule 39 个观测点参考

本表按模型声明列出 hook、额外参数和锁定上游的源码位置。所有 hook 的前两个参数均为 `call: Observation, core: Core`；表中只列其后的参数。逻辑前缀分别是 `tt_emule.device.core.cb`、`.noc` 和 `.compute`。

`Observation` 包含 sequence:u64、thread:u32、phase:u32；`Core` 包含 device:u64、x:u32、y:u32。phase 0/1 表示进入/退出，不能解释为失败/成功。调用参数按值采集，地址只作为数值；表中没有列出的参数不能视作已知零。

## CB（6 个）

声明来源：[模型文件](../models/tt_emule/src/hooks/cb.fg)。源码位置相对于锁定 tt-emule checkout；插桩后行号以本次 `catalog.json` 为准。

| Hook | 额外参数 | 上游位置 |
| --- | --- | --- |
| `cb_reserve_back` | `cb_id: u32, n: u32` | `include/jit_hw/api/cb_api.h:154` |
| `cb_push_back` | `cb_id: u32, n: u32` | `include/jit_hw/api/cb_api.h:184` |
| `cb_wait_front` | `cb_id: u32, n: u32` | `include/jit_hw/api/cb_api.h:225` |
| `cb_pop_front` | `cb_id: u32, n: u32` | `include/jit_hw/api/cb_api.h:259` |
| `get_write_ptr` | `cb_id: u32` | `include/jit_hw/api/cb_api.h:307` |
| `get_read_ptr` | `cb_id: u32` | `include/jit_hw/api/cb_api.h:312` |

## NOC（14 个）

声明来源：[模型文件](../models/tt_emule/src/hooks/noc.fg)。源码位置相对于锁定 tt-emule checkout；插桩后行号以本次 `catalog.json` 为准。

| Hook | 额外参数 | 上游位置 |
| --- | --- | --- |
| `noc_async_read` | `src_noc_addr: u64, dst_local_l1_addr: u32, size: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:317` |
| `noc_async_write` | `src_local_l1_addr: u32, dst_noc_addr: u64, size: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:345` |
| `noc_async_read_barrier` | `noc: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:535` |
| `noc_async_write_barrier` | `noc: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:538` |
| `noc_async_writes_flushed` | `noc: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:539` |
| `noc_async_atomic_barrier` | `noc: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:561` |
| `noc_semaphore_inc` | `noc_addr: u64, incr: u32, noc: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:776` |
| `noc_semaphore_set` | `val: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:725` |
| `noc_semaphore_wait` | `val: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:732` |
| `noc_semaphore_wait_min` | `min_val: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:754` |
| `noc_async_write_multicast` | `src_local_l1_addr: u32, dst_mcast_noc_addr: u64, size: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:479` |
| `noc_async_read_one_packet` | `src_noc_addr: u64, dst_local_l1_addr: u32, size: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:378` |
| `noc_async_write_one_packet` | `src_local_l1_addr: u32, dst_noc_addr: u64, size: u32` | `include/jit_hw/api/dataflow/dataflow_api.h:382` |
| `noc_inline_dw_write` | `addr: u64, val: u32, be: u8` | `include/jit_hw/api/dataflow/dataflow_api.h:988` |

## COMPUTE（19 个）

声明来源：[模型文件](../models/tt_emule/src/hooks/compute.fg)。源码位置相对于锁定 tt-emule checkout；插桩后行号以本次 `catalog.json` 为准。

| Hook | 额外参数 | 上游位置 |
| --- | --- | --- |
| `tile_regs_acquire` | 无 | `include/jit_hw/api/compute/reg_api.h:26` |
| `tile_regs_commit` | 无 | `include/jit_hw/api/compute/reg_api.h:37` |
| `tile_regs_wait` | 无 | `include/jit_hw/api/compute/reg_api.h:38` |
| `tile_regs_release` | 无 | `include/jit_hw/api/compute/reg_api.h:39` |
| `add_tiles` | `icb0: u32, icb1: u32, idst: u32` | `include/jit_hw/api/compute/common.h:771` |
| `sub_tiles` | `icb0: u32, icb1: u32, idst: u32` | `include/jit_hw/api/compute/common.h:789` |
| `mul_tiles` | `icb0: u32, icb1: u32, idst: u32` | `include/jit_hw/api/compute/common.h:807` |
| `pack_tile` | `idst: u32, ocb: u32` | `include/jit_hw/api/compute/common.h:829` |
| `pack_tile_block` | `ifrom_dst: u32, ocb: u32, ntiles: u32` | `include/jit_hw/api/compute/common.h:877` |
| `copy_tile` | `icb: u32, itile: u32, idst: u32` | `include/jit_hw/api/compute/common.h:972` |
| `tilize_block` | `icb: u32, ntiles: u32, ocb: u32` | `include/jit_hw/api/compute/tilize.h:67` |
| `untilize_block` | `icb: u32, ntiles: u32, ocb: u32` | `include/jit_hw/api/compute/untilize.h:48` |
| `exp_tile` | `idst: u32` | `include/jit_hw/api/compute/eltwise_unary/exp.h:136` |
| `recip_tile` | `idst: u32` | `include/jit_hw/api/compute/eltwise_unary/recip.h:28` |
| `sqrt_tile` | `idst: u32` | `include/jit_hw/api/compute/eltwise_unary/sqrt.h:42` |
| `relu_tile` | `idst: u32` | `include/jit_hw/api/compute/eltwise_unary/relu.h:17` |
| `relu_max_tile` | `idst: u32, param0: u32` | `include/jit_hw/api/compute/eltwise_unary/relu.h:32` |
| `relu_min_tile` | `idst: u32, param0: u32` | `include/jit_hw/api/compute/eltwise_unary/relu.h:69` |
| `leaky_relu_tile` | `idst: u32, slope: u32` | `include/jit_hw/api/compute/eltwise_unary/relu.h:89` |

## 如何解释命中次数

39 是不同 API 名的数量，51 是实际被插桩的实现位置数量。一个 API 可能有多个实现/转发重载，转发会形成嵌套 scope；一次业务操作也可能循环调用同一 API。不要把事件数直接换算成硬件事务数。进入/退出各产生事件，但不把它们算成两个观测点。

CB 的叶节点按 CB ID 区分；NOC 和 compute 目前各是每核心编号 0 的聚合叶节点，不是完整 NOC 通道、信号量或寄存器文件枚举。semaphore 指针没有被解析成稳定对象身份。需要更细分对象时，应同时扩展模型声明、对象注册、provider 和测试，而不是仅给报告增加标签。

## 从模型追到真实源码

1. 从表中的 hook 找到模型签名，确认声明了哪些参数和宽度。
2. 在本次验证目录的 `catalog.json` 查对应 API 的所有实现位置、原始签名和参数槽映射。
3. 在 `reference` 的事件中查看实际命中、phase、thread/core 与参数；未采集字段是 null。
4. 在正式 `results` 中按对象键和 hook 找到 Mount，检查 dispatch、记录和计数器校验。
5. 对照场景的目标数据检查。只有命中和输出存在，不能证明目标计算正确。

catalog 来自实际已插桩源码，包含哈希；不要拿旧报告中的 catalog 判断新工作树。新增 hook 时必须同步声明、探针、生成 adapter、结果校验和至少一个真实场景。操作流程见 [tt-emule 使用指南](tt-emule.md)，读图方法见 [报告阅读指南](tt-emule-reports.md)。
