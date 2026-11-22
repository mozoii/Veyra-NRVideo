"""Automatic pre-PR check: prevents local-only content from leaking upstream.

Usage:
    python check_pr_ready.py <branch>

Checks:
    1. Whether the branch was opened from origin/main (not from custom)
    2. Whether local-only files are carried along
    3. Whether the change size is reasonable
    4. Whether upstream features were deleted
    5. Whether the commit messages are adequate
"""
import re
import subprocess
import sys
from pathlib import Path

# Windows consoles default to GBK; force UTF-8 so the report never dies on a symbol.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

REPO = str(Path(__file__).resolve().parents[1])

# Local-only files that must never enter the upstream repository
LOCAL_ONLY = {
    "CUSTOM_LEDGER.md",
    "docs/VeyraNV_LOCAL_BUILD_NOTES.md",
    "docs/DOVI_WORK_HANDOFF.md",
    "docs/DOVI_P5_VERIFICATION.md",
    "docs/DOLBY_BL_EL_COMPOSITING.md",
    "tools/veyra-switch/make_switch.py",
    "tools/dovi_p5_probe/main.cpp",
    "tools/dovi_p5_probe/compare_raw.py",
}

# Local-only settings fields/switch names (warn if they appear in the diff)
LOCAL_MARKERS = [
    "doviP7Compose",
    "doviFollowAverage",
    "doviFollow",
]


def run(*args, check=False):
    p = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, errors="replace")
    if check and p.returncode != 0:
        print("  git failed:", p.stderr.strip()[:200])
        sys.exit(1)
    return p.stdout


def base_of(branch):
    """Find where this branch actually forks."""
    # Merge-base against custom vs merge-base against origin/main
    mb_custom = run("merge-base", "custom", branch).strip()
    mb_main = run("merge-base", "origin/main", branch).strip()
    return mb_custom, mb_main


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    branch = sys.argv[1]
    ok = True

    print(f"Checking branch: {branch}\n")

    # 1. Fork point
    mb_custom, mb_main = base_of(branch)
    main_head = run("rev-parse", "origin/main").strip()
    if mb_custom == mb_main:
        print("[1] [OK] Clean fork from upstream (no custom customizations)")
    else:
        print("[1] [FAIL] Branch is based on custom and carries local customizations!")
        print(f"     merge-base(custom)={mb_custom[:8]}  merge-base(origin/main)={mb_main[:8]}")
        ok = False

    # 2. Local-only files
    files = [l for l in run("diff", "--name-only", f"origin/main..{branch}").splitlines() if l]
    bad = [f for f in files if f in LOCAL_ONLY]
    if bad:
        print(f"[2] [FAIL] Local-only files carried along: {', '.join(bad)}")
        ok = False
    else:
        print(f"[2] [OK] No local-only files ({len(files)} files)")

    # 3. Size
    stat = run("diff", "--shortstat", f"origin/main..{branch}").strip()
    print(f"[3] [INFO]  {stat}")
    m = re.search(r"(\d+) insertions", stat)
    ins = int(m.group(1)) if m else 0
    if ins > 800:
        print(f"     [WARN]  More than 800 lines changed; the author review burden is heavy. Consider splitting.")

    # 4. Deleted upstream features
    deleted = [l for l in run("diff", "--name-status", f"origin/main..{branch}").splitlines()
               if l.startswith("D\t")]
    if deleted:
        print(f"[4] [WARN]  Deleted upstream files: {', '.join(l.split(chr(9))[1] for l in deleted)}")
        print("     Deleting upstream features needs explicit author consent; do not submit without it.")

    # 5. Local-only names
    diff_text = run("diff", f"origin/main..{branch}")
    hits = [k for k in LOCAL_MARKERS if k in diff_text]
    if hits:
        print(f"[5] [WARN]  Local-only switches appear in the diff: {', '.join(hits)}")
        print("     Confirm these are not fields that only exist locally.")

    # 6. Commit messages
    subjects = [l for l in run("log", "--format=%s", f"origin/main..{branch}").splitlines() if l]
    weak = [s for s in subjects if len(s) < 20 or s.lower() in ("fix", "update", "wip", "changed things")]
    if weak:
        print(f"[6] [WARN]  Commit messages too terse: {weak}")
    else:
        print(f"[6] [OK] {len(subjects)} commits, messages adequate")

    print()
    if ok:
        print("Conclusion: safe to open the PR")
    else:
        print("Conclusion: do not submit yet; fix the issues above")
        sys.exit(1)


if __name__ == "__main__":
    main()
