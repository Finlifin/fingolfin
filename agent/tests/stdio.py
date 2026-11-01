"""Drive the C agent process over its actual framed stdio transport."""

import json
from pathlib import Path
import subprocess
import sys

binary, fixture = sys.argv[1:]
package = Path(fixture).read_text()
manifest = json.loads(package)["manifest"]


def request(seq, method="session.status", generation=1, **extra):
    return {
        "jsonrpc": "2.0",
        "id": str(seq),
        "method": method,
        "params": {"version": 1, "session_id": "42", "generation": str(generation), **extra},
    }


def wire(value):
    return json.dumps(value, separators=(",", ":"))


load = request(
    2,
    "program.load",
    package=package,
    program_hash=manifest["program_source_hash"],
    model_hash=manifest["model_source_hash"],
)
good = wire(request(1))
invalid = [
    b"",
    b"{}",
    b"[]",
    b"null",
    b"{",
    b"\xff",
    good.encode() + b"\x00",
    (good + "{}").encode(),
    good.replace('"id":"1"', '"id":1').encode(),
    good.replace('"id":"1"', '"id":"01"').encode(),
    good.replace('"id":"1"', '"id":"18446744073709551616"').encode(),
    good.replace('"id":"1"', '"id":"0"').encode(),
    good.replace('"id":"1"', '"id":"1","id":"2"').encode(),
    good.replace('"id":"1"', '"id":"1","i\\u0064":"2"').encode(),
    good.replace('"version":1', '"version":1,"version":1').encode(),
    good.replace('"version":1', '"version":1.0').encode(),
    good.replace('"version":1', '"version":2').encode(),
    good.replace("session.status", "session.status\\u0000evil").encode(),
    wire(request(1, "unknown")).encode(),
    wire(request(1, extra=True)).encode(),
]
frames = invalid + [
    good.encode(),
    wire(load).encode(),
    wire(load).encode(),
    wire(request(3, "program.unload", generation=1, program_id="1")).encode(),
    wire(request(4)).encode(),
    wire(request(5, "program.unload", generation=2, program_id="1")).encode(),
    wire(
        request(6, "program.load", generation=3, package="{}", program_hash="", model_hash="")
    ).encode(),
    wire(request(7, "session.close", generation=3)).encode(),
    wire(request(8)).encode(),
]
result = subprocess.run(
    [binary, "--session-id", "42"],
    input=b"\n".join(frames) + b"\n",
    capture_output=True,
    timeout=30,
)
assert result.returncode == 0 and not result.stderr, result
replies = [json.loads(line) for line in result.stdout.splitlines()]
assert len(replies) == len(frames)
for reply in replies[: len(invalid)]:
    assert reply["id"] is None and reply["error"]["code"] == -32600, reply
replies = replies[len(invalid) :]
assert replies[0]["result"]["last_sequence"] == "1"
assert replies[0]["result"]["program_count"] == 0
assert replies[1]["result"]["program_id"] == "1" and replies[1]["result"]["generation"] == "2"
for i, seq in [(2, "2"), (3, "3")]:
    assert replies[i]["error"]["data"]["last_sequence"] == seq
    assert replies[i]["error"]["data"]["generation"] == "2"
assert replies[4]["result"]["program_count"] == 1
assert replies[5]["result"]["program_count"] == 0 and replies[5]["result"]["generation"] == "3"
assert replies[6]["error"]["data"]["generation"] == "3"
assert replies[7]["result"]["closed"] and replies[7]["result"]["generation"] == "4"
assert replies[8]["error"]["data"]["last_sequence"] == "7"
# EOF releases a live program; incomplete final request is never executed.
for data, status, count in [(wire(load).encode() + b"\n", 0, 1), (wire(load).encode(), 1, 0)]:
    p = subprocess.run([binary, "--session-id", "42"], input=data, capture_output=True, timeout=30)
    assert p.returncode == status and len(p.stdout.splitlines()) == count, p
# Oversize frame is drained; following valid request remains aligned.
p = subprocess.run(
    [binary, "--session-id", "42"],
    input=b" " * (64 * 1024 * 1024 + 1) + b"\n" + good.encode() + b"\n",
    capture_output=True,
    timeout=30,
)
assert p.returncode == 0, p
a, b = map(json.loads, p.stdout.splitlines())
assert a["error"]["code"] == -32600 and b["result"]["last_sequence"] == "1"
for value in ["0", "-1", "01", "18446744073709551616"]:
    p = subprocess.run([binary, "--session-id", value], capture_output=True, timeout=10)
    assert p.returncode == 2 and not p.stdout
print(
    "agent stdio: framing, strict envelopes, load/unload, replay, stale generations, close and EOF passed"
)


# Enumerate by identity, not internal slot order: unloading slot 0 and loading
# again must produce IDs 2,3, with no stale ID 1 or repeated entries.
def loading(seq, generation):
    return request(seq, "program.load", generation, package=package, program_hash="", model_hash="")


commands = [
    request(1, "program.list", 999),
    loading(2, 1),
    loading(3, 2),
    request(4, "program.unload", 3, program_id="1"),
    loading(5, 4),
    request(6, "program.list", 0),
    request(6, "program.list", 0),
    request(7, "session.close", 5),
    request(8, "program.list", 0),
]
p = subprocess.run(
    [binary, "--session-id", "42"],
    input="".join(wire(c) + "\n" for c in commands),
    text=True,
    capture_output=True,
    timeout=30,
)
assert p.returncode == 0 and not p.stderr, p
answers = [json.loads(line) for line in p.stdout.splitlines()]
assert len(answers) == len(commands)
assert answers[0]["result"]["programs"] == []
listing = answers[5]["result"]
assert listing["generation"] == "5" and listing["last_sequence"] == "6"
assert listing["program_count"] == 2
assert [item["program_id"] for item in listing["programs"]] == ["2", "3"]
for item in listing["programs"]:
    assert item["program_hash"] == manifest["program_source_hash"]
    assert item["model_hash"] == manifest["model_source_hash"]
    assert item["entry_count"] == len(manifest["entries"])
    assert {e["function"] for e in item["entries"]} == set(manifest["entries"])
    for entry in item["entries"]:
        original = manifest["entries"][entry["function"]]
        assert entry["hook"] == (original["hook"] or "")
        assert entry["helper_mask"] == sum(1 << helper for helper in original["helpers"])
        assert int(entry["frame_bytes"]) > 0
assert "programs" not in answers[6]["error"]["data"]
assert answers[6]["error"]["data"]["last_sequence"] == "6"
assert "programs" not in answers[8]["error"]["data"]
assert answers[8]["error"]["data"]["closed"]
print(
    "agent program.list: empty, live metadata, sorted IDs after reuse, replay and closed session passed"
)
