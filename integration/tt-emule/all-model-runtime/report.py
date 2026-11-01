#!/usr/bin/env python3
"""Render the combined runtime's verified hook/scenario matrix."""

import argparse, html, json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--results", type=Path, required=True)
a = p.parse_args()
out = a.results
rows = json.loads((out / "summary.json").read_text())
manifest = json.loads((out / "manifest.json").read_text())
labels = list(dict.fromkeys(x["scenario"] for x in rows))
if any(not x["passed"] or x["errors"] for x in rows):
    raise RuntimeError("failed evidence")
dispatches = sum(x["dispatches"] for x in rows)
records = sum(x["records"] for x in rows)
page = '<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>39 个 hook · 正式运行时验证</title><style>body{font:16px system-ui;background:#101723;color:#e0e8f3;padding:24px}table{border-collapse:collapse}th,td{padding:10px;border-bottom:1px solid #465368}a{color:#7ee7bd}p{max-width:1050px;line-height:1.7}.scroll{overflow:auto}th{position:sticky;top:0;background:#101723}</style><h1>39 个 hook · 同一个 C 运行时</h1>'
page += f'<p>6 个真实场景通过 · {dispatches:,} 次成功 dispatch · {records:,} 条逐条核对的输出 · 错误 0</p><p>真实 API → C factory → 生成 adapter → Session/Mount → 具名 .fg 程序。表格是各场景的实际调用次数，进入和退出分别调用；“—”明确表示该场景未命中。</p><p><a href="reference/index.html">独立诊断覆盖地图与事件时间线</a> · <a href="summary.json">正式执行原始结果</a> · <a href="provenance.json">源码哈希</a></p><div class="scroll"><table><tr><th>类别 / hook</th>'
page += "".join(f'<th><a href="{label}-test/0.log">{label}</a></th>' for label in labels) + "</tr>"
for point in manifest:
    page += "<tr><td>" + html.escape(point["group"] + " / " + point["name"]) + "</td>"
    for label in labels:
        x = next(x for x in rows if x["scenario"] == label and x["hook"] == point["hook"])
        page += (
            f'<td><a href="{label}-{point["hook"]}.json">'
            + (str(x["dispatches"]) if x["dispatches"] else "—")
            + "</a></td>"
        )
    page += "</tr>"
page += "</table></div><p>范围：单进程 core0 fixture，CB 限定 CB0；元数据及模型当前声明的参数经过验证。未声明参数、全部重载路径、跨 launch/device 生命周期和通用动态订阅仍待开发。此实现用于正确性验证，不作为性能验收。</p></html>"
(out / "index.html").write_text(page)
print(out / "index.html")
