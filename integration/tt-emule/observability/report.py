#!/usr/bin/env python3
"""Render a self-contained, filterable report from real emulator trace evidence."""

import argparse
import collections
import html
import json
import os
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--catalog", type=Path, required=True)
p.add_argument("--trace", type=Path, action="append", required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--failure", type=Path, action="append", default=[])
p.add_argument("--result", type=Path, action="append", default=[])
a = p.parse_args()
catalog = json.loads(a.catalog.read_text())
traces = [{"name": path.stem, **json.loads(path.read_text())} for path in a.trace]
ids = {point["id"] for point in catalog["points"]}
for trace in traces:
    assert not trace["dropped"], "Trace overflow: refuse to present incomplete evidence as complete"
    assert all(e["hook"] in ids and e["phase"] in [0, 1] for e in trace["events"])
    assert [e["sequence"] for e in trace["events"]] == list(range(1, len(trace["events"]) + 1))
coverage = collections.Counter(e["hook"] for t in traces for e in t["events"] if e["phase"] == 0)
results = [
    {"evidence": os.path.relpath(path.parent, a.output.parent), **json.loads(path.read_text())}
    for path in a.result
]
data = json.dumps(
    {
        "catalog": catalog,
        "traces": traces,
        "results": results,
        "failures": [json.loads(p.read_text()) for p in a.failure],
    },
    ensure_ascii=False,
).replace("<", "\\u003c")
page = """<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>tt-emule 真实观测报告</title><style>
body{font:16px system-ui;margin:0;background:#101723;color:#e0e8f3}main{max-width:1250px;margin:auto;padding:32px}h1{font-size:32px}p{line-height:1.7;color:#b8c7dc}.cards{display:flex;gap:16px;flex-wrap:wrap}.card{background:#1b283b;padding:20px;border-radius:12px;min-width:160px}.number{display:block;font-size:36px;color:#7ee7bd}select,input{background:#20324a;color:white;border:1px solid #55708e;padding:10px;border-radius:6px;margin:8px}table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:9px;border-bottom:1px solid #293d56}th{position:sticky;top:0;background:#1b283b}.hit{color:#7ee7bd}.miss{color:#a3adc0}code{font-size:13px}.scroll{max-height:550px;overflow:auto}details{padding:10px;background:#192538;margin:8px 0}.bar{height:7px;background:#63c9ff;display:inline-block}.badge{padding:3px 6px;border-radius:4px;background:#263c56}button{padding:10px;background:#3272a8;color:white;border:0;border-radius:6px;cursor:pointer}</style><main>
<h1>tt-emule · 真实调用可视化</h1><div id="results"></div><p>观测点是实际 API 实现；进入/退出是同一点的两个阶段。绿色表示真实测试已命中，灰色表示已插桩但本报告未覆盖。统计包含转发重载的嵌套调用，不等同于硬件事务数量。</p>
<div class="cards" id="cards"></div>
<div id="failures"></div><h2>观测覆盖地图</h2><p>CB 管理缓冲区；NOC 传输与屏障；compute 覆盖寄存器、搬运、算术、格式转换。点击源码签名查看实测参数槽的含义。</p>
<select id="category"><option value="">全部类别</option><option>cb</option><option>noc</option><option>compute</option></select><input id="search" placeholder="搜索 API 名称"><div id="catalog"></div>
<h2>事件时间线</h2><p>选择场景和观测点，逐行查看调用顺序、core、线程及参数。时间为相对首条记录的单调时钟读数；它包含探针开销，不是性能基准。线程不是 fiber/kernel 身份；退出也不自动等于操作成功。</p>
<select id="scenario"></select><select id="hook"><option value="">全部观测点</option></select><select id="phase"><option value="">进入 + 退出</option><option value="0">仅进入</option><option value="1">仅退出</option></select>
<div id="summary"></div><div class="scroll"><table><thead><tr><th>序号</th><th>时间 µs</th><th>API / 阶段</th><th>core / thread</th><th>参数槽 a / b / c</th></tr></thead><tbody id="events"></tbody></table></div>
<p>尚未覆盖：完整 run/launch/kernel/fiber 生命周期、可解析的 semaphore 对象身份、错误/超时结果。当前为诊断探针，未宣称全部点已连接正式 .fg 模型 adapter。</p>
<script id="data" type="application/json">DATA</script><script>
const d=JSON.parse(document.querySelector('#data').textContent), points=d.catalog.points, byId=new Map(points.map(p=>[p.id,p]));
const counts=new Map();let total=0;for(const t of d.traces)for(const e of t.events){total++;if(!e.phase)counts.set(e.hook,(counts.get(e.hook)||0)+1)}
function node(tag,text){const n=document.createElement(tag);n.textContent=text;return n}
for(const [label,value]of [['源码观测点',points.length],['实际命中',counts.size],['真实场景',d.traces.length],['事件记录',total]]){const n=node('div',label);n.className='card';const v=node('strong',value);v.className='number';n.append(v);document.querySelector('#cards').append(n)}
const explanations={
'Fingolfin_LayoutRoundTrip_Observed':'32×64 非均匀 BF16 数据 → tilize → 独立核对两张 tile 的面布局 → untilize → 逐元素还原检查（各 2048 个元素）。',
'Fingolfin_MulticastTwoReceivers_Observed':'核 (0,0) → multicast → 核 (1,0) 与 (2,0)：两个接收核各校验 512 个字，并向发送核确认。',
'CB_Boundary_Wraparound_NoViolation':'CB 环形边界：reserve → push → wait → pop，验证回绕场景。',
'TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter':'DRAM → reader → compute copy → writer → DRAM，运行原有数据校验。',
'Fingolfin_NocTransferAndSemaphore_Observed':'L1 → NOC read → NOC write：逐字检查 4 个结果；inline write 检查值 7；信号量检查值 1。',
'Fingolfin_ArithmeticAndActivation_Observed':'两张 BF16 输入 tile → 10 种算术/激活操作 → 输出 CB：每种检查 1024 个元素，exp 使用明确容差。'
};
for(const group of d.results)for(const result of group.results){const box=node('details','');box.open=true;const title=node('summary',`${result.passed?'✓ 通过':'✗ 失败'} · ${result.filter.split('.').pop()} · ${Number(result.seconds).toFixed(3)} s`);title.className=result.passed?'hit':'miss';box.append(title,node('p',explanations[result.filter.split('.').pop()]||result.reason));const link=node('a','查看原始 gtest 日志');link.href=group.evidence+'/0.log';link.style.color='#63c9ff';box.append(link);document.querySelector('#results').append(box)}
for(const group of d.failures)for(const failure of group.results){const row=node('p',`失败场景：${failure.filter} — ${failure.reason}（未计入通过覆盖；详见保存的日志）`);row.style.color='#ffb7a6';document.querySelector('#failures').append(row)}
function catalog(){const target=document.querySelector('#catalog');target.replaceChildren();for(const p of points){if(category.value&&p.category!==category.value||search.value&&!p.name.includes(search.value))continue;const row=node('details',''),sum=node('summary',`${counts.has(p.id)?'●':'○'} ${p.category} / ${p.name} — ${counts.get(p.id)||0} 次进入`);sum.className=counts.has(p.id)?'hit':'miss';row.append(sum,node('p',p.source));for(const v of p.variants)row.append(node('p',`行 ${v.line}: ${v.signature} | a/b/c = ${v.fields.map(f=>f||'未采集').join(' / ')}`));target.append(row)}}
const category=document.querySelector('#category'),search=document.querySelector('#search');category.onchange=search.oninput=catalog;catalog();
const scenario=document.querySelector('#scenario'),hook=document.querySelector('#hook'),phase=document.querySelector('#phase');d.traces.forEach((t,i)=>{const o=node('option',t.name);o.value=i;scenario.append(o)});points.forEach(p=>{const o=node('option',p.name);o.value=p.id;hook.append(o)});
function timeline(){const t=d.traces[Number(scenario.value)],all=t.events.filter(e=>(!hook.value||e.hook==hook.value)&&(phase.value===''||e.phase==phase.value)),body=document.querySelector('#events');body.replaceChildren();document.querySelector('#summary').textContent=`匹配 ${all.length} 条；展示前 2000 条。丢弃 ${t.dropped} 条。`;const start=BigInt(t.events[0]?.ns||0);for(const e of all.slice(0,2000)){const tr=document.createElement('tr');for(const text of [e.sequence,(Number(BigInt(e.ns)-start)/1000).toFixed(2),byId.get(e.hook).name+' / '+(e.phase?'退出':'进入'),`(${e.x},${e.y}) / T${e.thread}`,e.args.join(' / ')])tr.append(node('td',text));body.append(tr)}}
scenario.onchange=hook.onchange=phase.onchange=timeline;timeline();</script></main></html>""".replace(
    "DATA", data
)
a.output.write_text(page)
print(
    f'{len(catalog["points"])} points, {len(coverage)} hit, {sum(len(t["events"]) for t in traces)} events: {a.output}'
)
