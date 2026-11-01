#!/usr/bin/env python3
"""Format maintained sources; preserve historical reports and exact external patches."""

import argparse
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {".git", ".venv", "target", "build", "reports", "__pycache__"}


def sources():
    for p in ROOT.rglob("*"):
        if p.is_file() and not EXCLUDED.intersection(p.relative_to(ROOT).parts):
            yield p


def fg_whitespace(text):
    """Indent existing .fg lines without rewriting grammar or negative test tokens."""
    depth = 0
    result = []
    for line in text.splitlines():
        stripped = line.strip()
        syntax = re.sub(r'"(?:\\.|[^"\\])*"|--.*', "", stripped)
        before = max(0, depth - (1 if syntax.startswith("}") else 0))
        result.append("    " * before + stripped if stripped else "")
        depth = max(0, depth + syntax.count("{") - syntax.count("}"))
    return "\n".join(result).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for tool in ("cargo", "clang-format", "black", "cmake-format"):
        if not shutil.which(tool):
            parser.error(f"missing {tool}; see docs/development.md")
    paths = list(sources())
    commands = [["cargo", "fmt", "--all"] + (["--check"] if args.check else [])]
    groups = [
        (
            ["clang-format"] + (["--dry-run", "--Werror"] if args.check else ["-i"]),
            {".c", ".h", ".cpp", ".hpp"},
        ),
        (["black", "--quiet", "--workers", "1"] + (["--check"] if args.check else []), {".py"}),
    ]
    for prefix, suffixes in groups:
        selected = [str(p.relative_to(ROOT)) for p in paths if p.suffix in suffixes]
        if selected:
            if prefix[0] == "black":
                commands.extend(prefix + [path] for path in sorted(selected))
            else:
                commands.append(prefix + sorted(selected))
    cmake = [str(p.relative_to(ROOT)) for p in paths if p.name == "CMakeLists.txt"]
    if cmake:
        commands.append(["cmake-format", "--check" if args.check else "-i", *cmake])
    for cmd in commands:
        subprocess.run(cmd, cwd=ROOT, check=True)
    dirty = []
    for p in paths:
        if p.suffix == ".fg":
            old = p.read_text()
            new = fg_whitespace(old)
            if old != new:
                dirty.append(str(p.relative_to(ROOT)))
                if not args.check:
                    p.write_text(new)
    if args.check and dirty:
        raise SystemExit(".fg whitespace differs: " + ", ".join(dirty))
    print("Source formatting checked" if args.check else "Sources formatted")


if __name__ == "__main__":
    main()
