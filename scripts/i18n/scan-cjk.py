#!/usr/bin/env python3
"""Scan the repository for remaining CJK-bearing lines.

Usage: python3 -B scripts/i18n/scan-cjk.py [--out=PATH] [--json=PATH]

The scanner walks the first-party tree (skipping .git, build dirs, licenses,
third_party, docs/history, and the i18n catalog translation columns) and
prints one line per file/line with the CJK span, then writes the same data as
JSON for the conversion workflow. The catalog (i18n/catalog.json) is excluded
entirely: its zh columns are translated Chinese by design.
"""
import argparse
import json
import re
from pathlib import Path

CJK = re.compile(r'[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\uac00-\ud7af\u3000-\u303f\uff00-\uffef\u200c\u200d]+')
EXCLUDE_DIRS = {'.git', 'build-local', 'out', 'licenses', 'third_party', 'docs/history', 'handoffs'}
EXCLUDE_FILES = {'i18n/catalog.json', 'scripts/i18n/translate.json', 'i18n/translate.json', 'docs/WORKLOG.md', 'docs/HISTORY.md'}
SCAN_EXTS = {'.cpp', '.h', '.qml', '.py', '.js', '.md', '.txt', '.yml', '.yaml', '.cmake', '.json', '.cmd', '.bat', '.ps1', '.cs', '.sh', '.toml', '.xml', '.html', '.css', '.ts', '.mjs', '.cjs'}


def scan(root: Path):
    spans = {}  # span -> [files]
    lines = []
    for f in sorted(root.rglob('*')):
        if not f.is_file():
            continue
        rel = f.relative_to(root).as_posix()
        if rel in EXCLUDE_FILES:
            continue
        parts = rel.split('/')
        if any(p in EXCLUDE_DIRS for p in parts):
            continue
        if rel == 'docs/history' or rel.startswith('docs/history/'):
            continue
        if f.suffix.lower() not in SCAN_EXTS:
            continue
        try:
            text = f.read_text(encoding='utf-8', errors='strict')
        except Exception:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            for m in CJK.finditer(line):
                span = m.group(0).strip()
                if not span:
                    continue
                spans.setdefault(span, set()).add(rel)
                lines.append({'file': rel, 'line': i, 'text': line.strip()[:200], 'span': span})
    return spans, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=None)
    ap.add_argument('--json', default=None)
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent.parent
    spans, lines = scan(root)
    print(f"lines with CJK: {len(lines)}")
    print(f"unique spans: {len(spans)}")
    payload = {
        'root': str(root),
        'total_lines': len(lines),
        'unique_spans': len(spans),
        'spans': {s: sorted(f) for s, f in spans.items()},
        'lines': lines,
    }
    if args.json:
        out_json = Path(args.json)
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
        print(f"wrote {out_json}")
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        with open(args.out, 'w', encoding='utf-8') as fh:
            for ln in sorted(lines, key=lambda x: (x['file'], x['line'])):
                fh.write(f"{ln['file']}:{ln['line']}: {ln['text']}\n")
        print(f"wrote {args.out}")
    # also dump the unique span table
    print("\n== unique spans (most-seen first) ==")
    for span, files in sorted(spans.items(), key=lambda kv: -len(kv[1])):
        print(f"  x{len(files):3d}  {span}")


if __name__ == '__main__':
    main()
