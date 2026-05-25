#!/usr/bin/env python3
"""Check local Markdown links without requiring network access."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")


def markdown_files() -> list[Path]:
    files = []
    for path in ROOT.rglob("*.md"):
        rel = path.relative_to(ROOT)
        if rel.parts and rel.parts[0] in {"build", "results", "run", "zk_build", "hardhat/node_modules"}:
            continue
        if "node_modules" in rel.parts:
            continue
        files.append(path)
    return sorted(files)


def is_external(target: str) -> bool:
    return bool(re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target))


def check_file(path: Path) -> list[str]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8")
    headings = {
        re.sub(r"[^a-z0-9 -]", "", line.strip("# ").lower()).replace(" ", "-")
        for line in text.splitlines()
        if line.startswith("#")
    }
    for match in LINK_RE.finditer(text):
        raw = match.group(1).strip()
        if not raw or is_external(raw):
            continue
        target, _, anchor = raw.partition("#")
        target = unquote(target)
        if target:
            resolved = (path.parent / target).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                errors.append(f"{path.relative_to(ROOT)}: link escapes repo: {raw}")
                continue
            if not resolved.exists():
                errors.append(f"{path.relative_to(ROOT)}: missing link target: {raw}")
                continue
            if anchor and resolved.suffix == ".md":
                linked_text = resolved.read_text(encoding="utf-8")
                linked_headings = {
                    re.sub(r"[^a-z0-9 -]", "", line.strip("# ").lower()).replace(" ", "-")
                    for line in linked_text.splitlines()
                    if line.startswith("#")
                }
                if anchor.lower() not in linked_headings:
                    errors.append(f"{path.relative_to(ROOT)}: missing anchor in {target}: #{anchor}")
        elif anchor and anchor.lower() not in headings:
            errors.append(f"{path.relative_to(ROOT)}: missing local anchor: #{anchor}")
    return errors


def main() -> int:
    errors: list[str] = []
    for path in markdown_files():
        errors.extend(check_file(path))
    if errors:
        for error in errors:
            print(error)
        return 1
    print(f"[markdown-links] PASS ({len(markdown_files())} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
