#!/usr/bin/env python3
"""Validate local Markdown file links in maintained documentation."""

import re
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
paths = [ROOT / "README.md", *(ROOT / "docs").rglob("*.md")]
paths += [
    p
    for base in ("runtime", "agent", "cli", "sdk", "models", "integration")
    for p in (ROOT / base).rglob("*.md")
]
paths += [ROOT / "reports/README.md", ROOT / "reports/final-validation.md"]
errors = []
count = 0
for path in paths:
    if not path.is_file():
        errors.append(f"missing document: {path.relative_to(ROOT)}")
        continue
    text = re.sub(r"```.*?```", "", path.read_text(), flags=re.S)
    for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", text):
        target = target.strip().strip("<>")
        parsed = urlsplit(target)
        if parsed.scheme or not parsed.path:
            continue
        count += 1
        if not (path.parent / unquote(parsed.path)).exists():
            errors.append(f"{path.relative_to(ROOT)}: missing {target}")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Checked {len(paths)} documents and {count} local links")
