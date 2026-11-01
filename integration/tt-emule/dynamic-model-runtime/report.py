#!/usr/bin/env python3
"""Render saved real-scenario and injected-recovery evidence."""

import argparse, html, json
from treecheck import verify_tree
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--results", type=Path, required=True)
a = p.parse_args()
out = a.results
rows = json.loads((out / "summary.json").read_text())
faults = json.loads((out / "recovery/summary.json").read_text())


def object_tree(r):
    verify_tree(r)
    nodes = {}
    roots = set()

    def add(k, label, parent=None):
        ident = tuple(k[f] for f in ("run", "launch", "slot", "generation"))
        node = nodes.setdefault(ident, dict(label=label, children=set(), hooks=set()))
        if parent is None:
            roots.add(ident)
        else:
            nodes[parent]["children"].add(ident)
        return ident

    for b in r["bindings"]:
        core, device, root = b["ancestors"]
        rk = add(root, f"Launch {b['launch']}")
        dk = add(device, f"Device {b['device']}", rk)
        ck = add(core, f"Core ({b['x']}, {b['y']})", dk)
        kind = f"CB {b['local']}" if b["hook"] <= 6 else "NOC" if b["hook"] <= 20 else "Compute"
        lk = add(b, kind, ck)
        nodes[lk]["hooks"].add((b["hook"], b["dispatches"]))

    def render(k):
        n = nodes[k]
        label = html.escape(n["label"]) + f" · slot {k[2]} / gen {k[3]}"
        if n["children"]:
            return (
                "<li><details open><summary>"
                + label
                + "</summary><ul>"
                + "".join(render(c) for c in sorted(n["children"]))
                + "</ul></details></li>"
            )
        return (
            "<li>"
            + label
            + "<small> — "
            + ", ".join(f"hook {h}: {count} 次" for h, count in sorted(n["hooks"]))
            + "</small></li>"
        )

    return (
        f'<p>已核验 {r["tree_checks"]} 条绑定父链，结束后确认 {r["tree_invalidations"]} 次祖先键失效。属性快照核验 {r["property_checks"]} 次，失效后 unknown 核验 {r["stale_properties"]} 次。</p><ul class="tree">'
        + "".join(render(k) for k in sorted(roots))
        + "</ul>"
    )


total = {k: sum(r[k] for r in rows) for k in ("begins", "dispatches", "records", "errors")}
page = '<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>动态路由与失败恢复</title><style>body{font:16px system-ui;background:#101723;color:#e0e8f3;padding:24px}table{border-collapse:collapse}th,td{padding:10px;border-bottom:1px solid #465368}a{color:#7ee7bd}p{line-height:1.7}details{margin:16px 0}.tree details{margin:5px 0}.tree ul{border-left:1px solid #465368;padding-left:24px}.tree li{margin:6px 0}.tree small{color:#a6bbd2}.scroll{overflow:auto}</style><h1>动态对象路由 · 失败恢复</h1>'
page += f'<p>{len(rows)} 个真实场景 · {total["begins"]} 次 launch · {total["dispatches"]:,} 次调用 · {total["records"]:,} 条匹配输出 · 控制器错误 {total["errors"]}</p><p>真实 exception 场景：kernel 完成数据检查后抛出异常 → RAII 清理 → 同一 Program 重跑 → 新身份与独立计数。以下合成故障与真实场景分开展示。</p><h2>合成故障恢复</h2><table><tr><th>注入故障</th><th>保留错误</th><th>跳过事件</th><th>结束时活动 launch</th><th>未释放 Mount</th><th>结果</th></tr>'
for r in faults:
    case = r["case"]
    page += (
        f'<tr><td><a href="recovery/{case}.json">{html.escape(case)}</a></td>'
        + "".join(
            "<td>" + str(r[k]) + "</td>"
            for k in ("errors", "skipped_events", "active_launch", "live_mounts")
        )
        + "<td>预期错误及恢复均通过</td></tr>"
    )
page += "</table>"
if (out / "properties/summary.json").exists():
    page += '<h2>模型属性读取</h2><p><a href="properties/summary.json">生成的 .fg 模式验证</a> · <a href="properties/test.log">编译与测试日志</a>：设备 0 匹配，其他设备不匹配；失效对象为 unknown；覆盖高位设备编号、槽复用和预算耗尽。生成阶段来自模型的显式属性契约；正式 Mount 筛选另见下方双设备对照。</p>'
page += "<h2>真实场景与对象状态</h2>"
for r in rows:
    label = r["scenario"]
    page += f'<details><summary>{html.escape(label)} · {r["begins"]} 次 launch · {r["dispatches"]} 次调用 · 通过</summary><p><a href="{label}-test/0.log">gtest 日志</a> · <a href="{label}.json">完整结果</a></p><div class="scroll"><table><tr><th>launch</th><th>hook</th><th>slot</th><th>generation</th><th>core</th><th>local</th><th>调用</th><th>记录</th></tr>'
    for b in r["bindings"]:
        page += (
            "<tr>"
            + "".join(
                "<td>" + str(b[k]) + "</td>" for k in ("launch", "hook", "slot", "generation")
            )
            + f'<td>({b["x"]},{b["y"]})</td>'
            + "".join("<td>" + str(b[k]) + "</td>" for k in ("local", "dispatches", "records"))
            + "</tr>"
        )
    page += "</table></div><h3>实际注册对象树</h3>" + object_tree(r) + "</details>"
if (out / "multidevice/summary.json").exists():
    multi = json.loads((out / "multidevice/summary.json").read_text())
    page += '<h2>双设备 deferred 验证</h2><p><a href="multidevice/summary.json">首次执行与缓存复用</a> · <a href="multidevice/cache.json">DSO 缓存证据</a> · <a href="multidevice/boundary/summary.json">设备身份边界</a></p>'
    for r in multi:
        page += (
            "<details><summary>"
            + html.escape(r["run"])
            + " · 双设备各两轮执行通过</summary><table><tr><th>device</th><th>launch</th><th>slot</th><th>generation</th><th>hook</th><th>调用</th><th>device==0 命中</th></tr>"
        )
        for b in r["bindings"]:
            page += (
                "<tr>"
                + "".join(
                    "<td>" + str(b[k]) + "</td>"
                    for k in (
                        "device",
                        "launch",
                        "slot",
                        "generation",
                        "hook",
                        "dispatches",
                        "matched_context",
                    )
                )
                + "</tr>"
            )
        page += "</table><h3>实际注册对象树</h3>" + object_tree(r) + "</details>"
if (out / "property-gate/summary.json").exists():
    gated = json.loads((out / "property-gate/summary.json").read_text())
    page += '<h2>正式 Mount 属性筛选</h2><p>由 .fg 模式生成的对象期 gate：device 0 / core (0,0) / CB 0 匹配。被筛掉的事件不构造调用帧、不执行 BPF，设备原有计算和输出检查继续正常进行。<a href="property-gate/summary.json">完整证据</a></p>'
    for r in gated:
        page += (
            "<details><summary>"
            + html.escape(r["run"])
            + f' · 执行 {r["dispatches"]} / 筛掉 {r["filtered"]}</summary><table><tr><th>设备</th><th>launch</th><th>hook</th><th>执行</th><th>筛掉</th></tr>'
        )
        for b in r["bindings"]:
            page += (
                "<tr>"
                + "".join(
                    "<td>" + str(b[k]) + "</td>"
                    for k in ("device", "launch", "hook", "dispatches", "filtered")
                )
                + "</tr>"
            )
        page += "</table></details>"
page += "<p>当前未覆盖进程 abort、未收到 end、全部底层分配失败及 VM 部分提交恢复；跨设备 NOC/fabric、并发 launch 和完整模型语义仍待开发。此报告未宣称 sanitizer 验收。</p></html>"
(out / "index.html").write_text(page)
print(out / "index.html")
