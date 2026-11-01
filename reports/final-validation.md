# 收尾验证

本记录对应文档重整与源码统一格式化后的首次提交。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Rust 回归 | 全部通过 | [测试日志](final-validation-1/rust-tests.log) |
| Clippy | 全目标、warnings 作为错误，通过 | [日志](final-validation-1/clippy.log) |
| C 完整执行链 | 24/24 通过，包含包、VM、Agent、CLI、Mount | [CTest](final-validation-1/c-tests.log) |
| 源码格式 | rustfmt、clang-format、Black、cmake-format、`.fg` 空白检查通过 | [日志](final-validation-1/format.log) |
| 文档 | 正式文档与组件指南的本地文件链接检查通过 | [日志](final-validation-1/docs.log) |
| tt-emule | 新目录完整 18 阶段通过 | [阶段](final-validation-1/stages.json) · [可视化](final-validation-1/tt-emule/index.html) |
| 设备 1 策略 | 首次及缓存执行均选择设备 1、筛掉设备 0 | [结果](final-validation-1/device1/summary.json) |

命令：`cargo test --locked --quiet`、`cargo clippy --locked --all-targets -- -D warnings`、`ctest --test-dir /tmp/fg-final-c-1 --output-on-failure`、`python3 development/format.py --check`、`python3 development/check_docs.py`。C 配置见 [CMakeCache](final-validation-1/c-build-config.txt)，tt-emule 的完整参数见 stages.json。

设备 1 策略使用 [独立 `.fg` 条件](final-validation-1/device1/policy.fg)，验证构建器不依赖固定设备 0 或固定属性读取数量。它仍是构建时选择，不宣称运行中热加载。

格式化覆盖维护中的 Rust、C/C++、Python、CMake 源码；`.fg` 只做不改变 token 的空白整理。外部补丁、历史报告和生成证据保持原字节，不改写旧哈希。[当前源码哈希](final-validation-1/source-hashes.json) 与 [摘要](final-validation-1/summary.json) 便于定位本轮输入。

未在本轮重新执行整体仿真器 sanitizer 或浏览器截图验收。当前能力边界见 [能力说明](../docs/development-status.md)，本记录不把原始计划的长期目标宣称为已完成。
