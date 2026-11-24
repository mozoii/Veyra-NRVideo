#!/usr/bin/env python3
"""Apply a Chinese->English span mapping to first-party files.

Usage:
    python3 -B scripts/i18n/translate-cjk.py --map mapping.json [--root .] [--dry-run]

The mapping is a JSON object whose keys are exact CJK spans and whose values
are the English replacement. A special value "KEEP" means leave the span
untouched (language names, vendor process names, test fixtures, legal text);
"SKIP" is the same thing written explicitly. Keys present in the mapping are
required to be found exactly as written; any span whose value is not "KEEP"
must exist in the files.
"""
import argparse
import json
import sys
from pathlib import Path

EXCLUDE_DIRS = {'.git', 'build-local', 'out', 'licenses', 'third_party', 'docs/history', 'handoffs'}
EXCLUDE_FILES = {'i18n/catalog.json', 'docs/WORKLOG.md', 'docs/HISTORY.md'}
SCAN_EXTS = {'.cpp', '.h', '.qml', '.py', '.js', '.md', '.txt', '.yml', '.yaml', '.cmake', '.json', '.cmd', '.bat', '.ps1', '.cs', '.sh', '.toml', '.xml', '.html', '.css', '.ts', '.mjs', '.cjs'}


def iter_files(root: Path):
    for f in sorted(root.rglob('*')):
        if not f.is_file():
            continue
        rel = f.relative_to(root).as_posix()
        parts = rel.split('/')
        if any(p in EXCLUDE_DIRS for p in parts):
            continue
        if rel in EXCLUDE_FILES:
            continue
        if f.suffix.lower() not in SCAN_EXTS:
            continue
        yield f, rel


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', required=True)
    ap.add_argument('--root', default='.')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()
    root = Path(args.root).resolve()
    mapping = json.loads(Path(args.map).read_text(encoding='utf-8'))
    files_changed = 0
    files_total = 0
    span_count = 0
    total_replaced = 0
    unmatched = []
    for span, replacement in mapping.items():
        if replacement in ('KEEP', 'SKIP'):
            continue
        for f, rel in iter_files(root):
            try:
                text = f.read_text(encoding='utf-8', errors='strict')
            except Exception:
                continue
            if span not in text:
                continue
            n = text.count(span)
            if n == 0:
                continue
            span_count += n
            if args.dry_run:
                print(f"DRY {rel}: span x{n}: {span!r} -> {replacement!r}")
                continue
            text = text.replace(span, replacement)
            f.write_text(text, encoding='utf-8')
            files_changed += 1
            total_replaced += n
    print(f"changed files: {files_changed}")
    print(f"replaced occurrences: {total_replaced}")
    # inventory: spans from the mapping never found in any file
    for span, replacement in mapping.items():
        if replacement in ('KEEP', 'SKIP'):
            continue
        found = False
        for f, rel in iter_files(root):
            try:
                text = f.read_text(encoding='utf-8', errors='strict')
            except Exception:
                continue
            if span in text:
                found = True
                break
        if not found:
            unmatched.append(span)
    if unmatched:
        print(f"spans never found after replacement ({len(unmatched)}):")
        for span in unmatched[:100]:
            print(f"  {span!r}")


if __name__ == '__main__':
    main()
