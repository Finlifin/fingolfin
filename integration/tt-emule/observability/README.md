# tt-emule 观测地图与可视化测试

39 个不同 API 观测点，51 个源码实现位置：CB 6、NOC 14、compute 19。进入/退出是同一观测点的两个阶段，不计作两个点。准备脚本检查固定 tt-emule revision，记录每处源码签名、行号、输入文件哈希和采集参数，生成可审查补丁。

```sh
python3 integration/tt-emule/observability/prepare.py \
  --emule /tmp/fingolfin-tt-emule --output /tmp/fg-observation-new --apply
python3 integration/tt-emule/observability/run.py \
  --metal /tmp/fingolfin-tt-metal --build /tmp/fingolfin-emule-build \
  --catalog /tmp/fg-observation-new/catalog.json --output reports/observation-new
```

必须使用新输出目录；已经应用探针时准备脚本拒绝重复插桩，直接使用已保存 catalog 运行即可。默认跑六个真实场景：CB wraparound、DRAM reader→compute copy→DRAM writer、NOC/信号量、算术/激活、布局转换、多核 multicast。gtest 失败/跳过/零测试、trace 丢弃、必需观测点缺失或线程内 scope 不配对都会失败。生成的 `index.html` 不依赖网络，可直接在浏览器打开；包含类别/API 筛选、命中/未命中地图和场景事件时间线。

观测模型声明位于 `models/tt_emule`。本目录的 C trace 是独立诊断路径；39 点正式 `.fg` adapter/Session/Mount 执行链在 `../dynamic-model-runtime`，统一验证入口为 `../verify.py`。两种回调不可同时 preload。

RAII scope 记录进入及离开（也可能由异常展开触发），不代表操作成功。标量参数含 CB ID/pages、NOC 地址/长度、tile/dst/CB 索引等，具体槽映射在 catalog。未采集字段为 null；回调槽填 0 不等于已知零。semaphore 原生宿主指针不作为稳定对象 ID 导出；该身份尚未解析。core 来自执行上下文，线程号是探针内编号，不是 kernel/fiber ID。转发重载会产生嵌套调用，因此事件次数不等于硬件事务数量。

探针使用固定 65,536 条缓冲、mutex 和单调时钟，退出时写 JSON；它会扰动执行时间，不用于性能验收，也不提供崩溃时完整记录。报告最多显示筛选后的前 2,000 条，完整原始 JSON 保留。当前报告明确区分已插桩与真实命中，未覆盖点必须继续补场景。

## 新场景准备与复现

在原有 tt-emule 集成构建基础上，依次向锁定的 tt-metal 源码应用四个测试补丁，向 tt-emule 应用 exp/sqrt 兼容补丁，再构建测试目标。使用绝对路径替换下例仓库根目录；补丁只应用一次。

```sh
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/observability/noc-scenario.patch
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/observability/compute-scenario.patch
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/observability/layout-scenario.patch
git -C /tmp/fingolfin-tt-metal apply /home/fin/apps/fingolfin/integration/tt-emule/observability/multicast-scenario.patch
git -C /tmp/fingolfin-tt-emule apply /home/fin/apps/fingolfin/integration/tt-emule/observability/exp-sqrt-compat.patch
cmake --build /tmp/fingolfin-emule-build --target unit_tests_api -j2
```

然后执行上文 `run.py`。四个 `.cpp` 文件保存可审阅的场景源码；补丁包含实际安装位置和先后依赖。算术场景同时包含 exp/sqrt 头文件，需要兼容补丁为局部 `p_setrwc` 补齐与公共定义一致的 `SET_ABD_F`。

- NOC：校验 4 个搬运结果、inline write 值 7、信号量值 1；覆盖单包读写、屏障与等待。
- 算术：输入 BF16 全 1，执行 add/sub/mul/exp/recip/sqrt/relu/relu_max/relu_min/leaky_relu；每种检查 1024 个输出元素。exp 接受明确的 BF16 量化容差，其余精确匹配。当前输入未覆盖负数、特殊浮点值等边界。
- 布局转换：32×64 非均匀 BF16 数据，每个元素使用不同的有限值；独立校验 tilize 的两张 tile 面布局，再检查 untilize 还原，共两次各 2048 个元素检查。中间使用 CB token 同步，避免只检查往返结果而遗漏互相抵消的布局错误。
- Multicast：发送核 (0,0)，接收核 (1,0)/(2,0)；接收核初始化后才允许发送，每个接收核检查全部 512 个字，再向发送核确认。运行器要求三个核都出现观测记录。
- 报告首页显示每个场景的测试结果、耗时、校验内容和日志链接；随后可浏览覆盖地图与事件时间线。

最新保存证据见 `reports/tt-emule-observability-3`。六场景覆盖全部 39 个 API 观测点；这是 API 命中覆盖，不是全部重载、参数组合、异常路径或多设备覆盖。

另有 `../cb-wide-count.patch` 修复历史 CB 计算测试中的 size_t 重载歧义，超出 uint32 范围会终止；该补丁不代表历史测试已经修复。应用后历史场景仍因 `riscv_wait` 缺失和指针参数宽度约束失败，退出时还出现 double-free。失败证据单独保存，不计入通过覆盖；新场景报告取自包含该补丁的工作树。
