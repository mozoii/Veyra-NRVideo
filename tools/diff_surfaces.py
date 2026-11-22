"""Generate the switch manifest: automatically find every file that differs between the custom build and the author edition.

Usage:
    python tools/diff_surfaces.py --author <author package> --custom <build directory> --package <program package>

Output: the file manifest that must go into the switch script, with the .orig / .p5 backups generatable directly.
This is a required step in the upgrade flow: newly added files (new shaders, new DLLs, new directories) are also found.
"""
import argparse
import hashlib
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# Paths compared (relative to package root), globs supported
WATCH = [
    "veyra_qml_ui.exe",
    "shaders/**/*.dxil",
    "shaders/**/*.cso",
    "qml/**/*.qml",
    "qml/**/*.js",
    "runtime/config/**/*",
]


def sha(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def collect(root: Path) -> dict[str, Path]:
    """Collect all watched files under root -> {relative path: absolute path}"""
    out = {}
    for pattern in WATCH:
        for p in root.glob(pattern):
            if p.is_file():
                out[str(p.relative_to(root)).replace("\\", "/")] = p
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--author", required=True, help="Author edition package directory")
    ap.add_argument("--custom", required=True, help="This repo build directory (with custom outputs)")
    ap.add_argument("--package", required=True, help="Actual running program package directory")
    args = ap.parse_args()

    author = Path(args.author).resolve()
    custom = Path(args.custom).resolve()
    pkg = Path(args.package).resolve()

    a_files = collect(author)
    c_files = collect(custom)

    print("=" * 70)
    print("Switch manifest: custom build vs author edition")
    print("=" * 70)
    print(f"Author package: {author}  ({len(a_files)} watched files)")
    print(f"Build directory: {custom}  ({len(c_files)} watched files)")
    print(f"Program package: {pkg}")
    print()

    all_paths = sorted(set(a_files) | set(c_files))
    different = []
    only_author = []
    only_custom = []

    for rel in all_paths:
        in_a = rel in a_files
        in_c = rel in c_files
        if in_a and in_c:
            if sha(a_files[rel]) != sha(c_files[rel]):
                different.append(rel)
        elif in_a:
            only_author.append(rel)
        else:
            only_custom.append(rel)

    print(f"[A] Different content, must be switched: {len(different)} files")
    for r in different:
        print(f"     {r}")

    print(f"\n[B] Only in the author package: {len(only_author)} files")
    for r in only_author[:20]:
        print(f"     {r}")
    if len(only_author) > 20:
        print(f"     ... and {len(only_author) - 20} more")

    print(f"\n[C] Only in the custom build: {len(only_custom)} files")
    for r in only_custom[:20]:
        print(f"     {r}")
    if len(only_custom) > 20:
        print(f"     ... and {len(only_custom) - 20} more")

    print()
    print("=" * 70)
    print("Conclusion")
    print("=" * 70)
    total = len(different) + len(only_author) + len(only_custom)
    if total == 0:
        print("  The two versions are identical; no switch script needed.")
        return 0

    print(f"  {len(different)} files need switching (different content).")
    print(f"  Plus {len(only_author)} only in the author edition and {len(only_custom)} only in the custom edition.")
    print()
    print("  [A] files must all be covered by the switch script.")
    print("  [B] and [C] usually do not need switching, but confirm they will not cause version mixing")
    print("     (e.g. a shader the author added that the custom exe references must be handled too.)")

    # Check whether the .orig / .p5 backups for these files exist in the package
    print()
    print("=" * 70)
    print("Backup status in the package")
    print("=" * 70)
    missing = []
    for r in different:
        base = pkg / r
        for suffix in (".orig", ".p5"):
            bak = base.with_name(base.name + suffix)
            if not bak.exists():
                missing.append(f"{r}{suffix}")
    if missing:
        print(f"  Missing {len(missing)} backups (the switch script will fail because of this):")
        for m in missing[:15]:
            print(f"     {m}")
    else:
        print("  All different files have .orig / .p5 backups")

    return 0


if __name__ == "__main__":
    sys.exit(main())
