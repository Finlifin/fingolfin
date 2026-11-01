"""Exercise the actual C CLI, including errors with no success output."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary, fixture = sys.argv[1:]


def run(args, code):
    result = subprocess.run([binary, *args], capture_output=True, text=True, timeout=15)
    assert result.returncode == code, result
    if code:
        assert not result.stdout and result.stderr, result
    return result


package = json.loads(Path(fixture).read_text())
manifest = package["manifest"]
result = json.loads(run(["inspect", fixture], 0).stdout)
assert result["format_version"] == 1
for key in ["program_source_hash", "model_source_hash"]:
    assert result[key] == manifest[key]
assert {entry["function"] for entry in result["entries"]} == set(manifest["entries"])
for entry in result["entries"]:
    expected = manifest["entries"][entry["function"]]
    assert entry["helper_mask"] == sum(1 << helper for helper in expected["helpers"])
    assert entry["frame_bytes"] > 0 and entry["elf_bytes"] > 0
pins = [
    "--program-hash",
    result["program_source_hash"],
    "--model-hash",
    result["model_source_hash"],
]
assert json.loads(run(["inspect", fixture, *pins], 0).stdout) == result
for option in ["--program-hash", "--model-hash"]:
    run(["inspect", fixture, option, "0" * 64], 1)
    run(["inspect", fixture, option, "invalid"], 1)
    run(["inspect", fixture, option], 2)
    run(["inspect", fixture, option, "0" * 64, option, "0" * 64], 2)
for args in [[], ["unknown"], ["inspect"], ["inspect", fixture, "--unknown", "x"]]:
    run(args, 2)
run(["--help"], 0)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "bad.fgo"
    run(["inspect", str(path)], 1)
    raw = Path(fixture).read_bytes()
    for data in [
        b"",
        b"{}",
        raw[: len(raw) // 2],
        raw + b"garbage",
        raw.replace(b"FINGOLFIN-FGO", b"INVALID-MAGIC", 1),
    ]:
        path.write_bytes(data)
        run(["inspect", str(path)], 1)
print("C fgctl: metadata, source pins, malformed packages, usage and I/O errors passed")
