"""End-to-end C CLI -> child transport -> C Agent, plus bad peer checks."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

cli, agent, fixture = sys.argv[1:]


def run(commands, transport=None):
    return subprocess.run(
        [cli, "session", "42", "--", *(transport or [agent, "--session-id", "42"])],
        input=commands,
        text=True,
        capture_output=True,
        timeout=40,
    )


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "package with spaces.fgo"
    path.write_bytes(Path(fixture).read_bytes())
    result = run(f"status\nload {path}\nlist\nunload 1\nlist\nclose\n")
    assert result.returncode == 0 and not result.stderr, result
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    assert [r["id"] for r in replies] == list(map(str, range(1, 7)))
    assert [r["result"]["generation"] for r in replies] == ["1", "2", "2", "3", "3", "4"]
    assert replies[2]["result"]["programs"][0]["program_id"] == "1"
    assert replies[4]["result"]["programs"] == []
    assert replies[5]["result"]["closed"]
    # Remote errors are visible and make the overall CLI fail, while allowing
    # subsequent status queries in the same live connection.
    result = run("unload 999\nstatus\n")
    assert result.returncode == 1, result
    first, second = map(json.loads, result.stdout.splitlines())
    assert "error" in first and second["result"]["last_sequence"] == "2"
    for commands in [
        "unknown\n",
        "status",
        "x" * 4096 + "\n",
        "unload 01\n",
        f"load {path}.missing\n",
        "status\x00ignored\n",
    ]:
        result = run(commands)
        assert result.returncode == 1 and not result.stdout and result.stderr, result
    path.write_text("{}")
    result = run(f"load {path}\n")
    assert result.returncode == 1 and not result.stdout, result

valid = {
    "jsonrpc": "2.0",
    "id": "1",
    "result": {
        "session_id": "42",
        "last_sequence": "1",
        "generation": "1",
        "program_id": "0",
        "program_count": 0,
        "closed": False,
    },
}
bad = [
    b"{}\n",
    b"not json\n",
    json.dumps(valid).replace('"id": "1"', '"id": "2"').encode() + b"\n",
    json.dumps(valid).replace('"session_id": "42"', '"session_id": "43"').encode() + b"\n",
    json.dumps(valid).replace('"generation": "1"', '"generation": "0"').encode() + b"\n",
    json.dumps(valid).replace('"id": "1"', '"id": "1", "id": "1"').encode() + b"\n",
    json.dumps(valid).encode(),
    b"\xff\n",
]
for data in bad:
    source = (
        f"import sys; sys.stdin.readline(); sys.stdout.buffer.write({data!r}); sys.stdout.flush()"
    )
    result = run("status\n", [sys.executable, "-c", source])
    assert result.returncode == 1 and not result.stdout and result.stderr, result
result = run("status\n", ["/nonexistent/fg-agent"])
assert result.returncode == 1 and result.stderr
result = run("status\n", [sys.executable, "-c", "raise SystemExit(7)"])
assert result.returncode == 1
print(
    "C CLI session: live command sequence, paths, local/remote errors, bad responses and child failures passed"
)
