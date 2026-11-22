#!/usr/bin/env python3
"""Generate the two-version switch script from the actual differences between the author files and this repo's custom build output.

Why this exists
---------------
The set of files a switch script replaces can differ per upstream version: this repo may have changed new shaders, added interface files, or an upstream change may have made a shader that was byte-identical before differ now. Copying the previous script would miss files, and the consequence of a miss is "two versions running mixed together" (for example, after switching to the author edition, a shader may still be ours).

So first compare file by file, then generate the script from the comparison. Run it once per upgrade.

Comparison scope (recursive, including subdirectories)
------------------------------------------------------
    veyra_qml_ui.exe        package directory\\veyra_qml_ui.exe    vs build directory\\veyra_qml_ui.exe
    shaders\\**\\*.dxil      package directory\\shaders\\**          vs build directory\\shaders\\**
                            (includes the shaders\\dis\\ subdirectory; 44 total: 25 top-level + 19 in dis)
    qml\\Veyra\\**          package directory\\qml\\Veyra\\**       vs source repository\\qml\\Veyra\\**

Files that are byte-identical **do not** enter the switch set (no need to switch them). Files that differ: non-interface files are copied individually (backed up as `*.orig` / `*.p5`); interface is synced as a whole directory via `robocopy /MIR` (backed up as `qml.orig\\Veyra` / `qml.p5\\Veyra`), because interface files reference each other, so replacing them one by one is meaningless.

Usage
-----
    # Show the differences and list the files entering the switch set (change nothing)
    python make_switch.py --package <package dir> --build <build dir> --repo <source dir>

    # Back up + generate the two .cmd files + make the custom build active
    python make_switch.py --package ... --build ... --repo ... --apply

Prerequisite: `--package` should currently be the **author edition** (just overwritten by a new version). If it is already the custom edition, the comparison is meaningless and the tool detects and warns.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys

EOL = '\r\n'


def sha(path: str) -> str | None:
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None


def size(path: str) -> int:
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def walk_rel(root: str, exts: tuple[str, ...] | None = None) -> dict[str, str]:
    """Recursively collect relative path -> absolute path."""
    out: dict[str, str] = {}
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            if exts and not f.lower().endswith(exts):
                continue
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root)] = p
    return out


def collect_diff(package: str, build: str, repo: str):
    """Return (non-interface diffs, interface diffs, notes). A diff entry is (relative path, author file, custom output)."""
    other: list[tuple[str, str, str]] = []
    qml: list[tuple[str, str, str]] = []
    notes: list[str] = []

    # --- main program ---
    a = os.path.join(package, 'veyra_qml_ui.exe')
    b = os.path.join(build, 'veyra_qml_ui.exe')
    if not os.path.isfile(b):
        notes.append('* No veyra_qml_ui.exe in the build directory; compile first')
    elif not os.path.isfile(a):
        notes.append('* No veyra_qml_ui.exe in the package directory')
    elif sha(a) == sha(b):
        notes.append('* The main program is byte-identical to the build output; confirm the package directory is the author edition and that the build is correct')
    else:
        other.append(('veyra_qml_ui.exe', a, b))

    # --- shaders (recursive, including the dis\ subdirectory) ---
    fa = walk_rel(os.path.join(package, 'shaders'), ('.dxil',))
    fb = walk_rel(os.path.join(build, 'shaders'), ('.dxil',))
    same = 0
    for rel in sorted(set(fa) | set(fb)):
        if rel not in fb:
            notes.append('* Shaders in the package but not the build: shaders\\%s' % rel)
            continue
        if rel not in fa:
            notes.append('* Shaders in the build but not the package: shaders\\%s' % rel)
            continue
        if sha(fa[rel]) != sha(fb[rel]):
            other.append((os.path.join('shaders', rel), fa[rel], fb[rel]))
        else:
            same += 1
    n_top = sum(1 for r in set(fa) & set(fb) if os.sep not in r)
    notes.append('Compared %d shaders (%d top-level + %d in dis): %d identical, %d different'
                 % (len(set(fa) & set(fb)), n_top, len(set(fa) & set(fb)) - n_top,
                    same, len(other) - (1 if any(r[0] == 'veyra_qml_ui.exe' for r in other) else 0)))

    # --- interface (whole-directory sync; list only the genuinely different files) ---
    qa = os.path.join(package, 'qml', 'Veyra')
    qb = os.path.join(repo, 'qml', 'Veyra')
    if not os.path.isdir(qb):
        notes.append('* No qml\\Veyra in the source repository')
    else:
        ga, gb = walk_rel(qa), walk_rel(qb)
        for rel in sorted(set(ga) | set(gb)):
            av, bv = ga.get(rel), gb.get(rel)
            if bv is None:
                notes.append('Interface: missing in the custom edition, will be removed by the switch: %s' % rel)
                qml.append((rel, av or '', ''))
            elif av is None:
                notes.append('Interface: added in the custom edition: %s' % rel)
                qml.append((rel, '', bv))
            elif sha(av) != sha(bv):
                qml.append((rel, av, bv))
        notes.append('Compared %d interface files: %d different (whole-directory sync on switch, not one by one)'
                     % (len(set(ga) | set(gb)), len(qml)))
    return other, qml, notes


def write_utf8_crlf(path: str, text: str) -> None:
    with open(path, 'wb') as f:
        f.write(text.replace('\n', EOL).encode('utf-8'))


def write_utf8(path: str, text: str, bom: bool = False) -> None:
    enc = 'utf-8-sig' if bom else 'utf-8'
    with open(path, 'wb') as f:
        f.write(text.replace('\n', EOL).encode(enc))


def build_cmd(is_p5: bool, other: list[tuple[str, str, str]], qml_diff) -> str:
    suffix = '.p5' if is_p5 else '.orig'
    src_dir = 'qml.p5' if is_p5 else 'qml.orig'
    title = ('P5 / HDR custom build (this repo: correct Dolby Vision Profile 5 conversion, HDR luminance curve / '
             'seven-step presets / intensity / monitor peak / HDR10 metadata)' if is_p5 else
             'Author complete edition (author release as-is, including Remote Play / Moonlight / Xbox / FSR / XeSS)')
    files = [rel for rel, _a, _b in other]

    L: list[str] = ['@echo off', 'chcp 65001 >nul', 'setlocal', 'cd /d "%~dp0"', '',
                    'rem ============================================================================',
                    'rem  ' + title,
                    'rem ============================================================================', '',
                    'rem --- Pre-flight check: any replacement while the program is running may only half-succeed, so block it first ---',
                    'tasklist /fi "imagename eq veyra_qml_ui.exe" 2>nul | find /i "veyra_qml_ui.exe" >nul',
                    'if not errorlevel 1 (',
                    '  echo [Error] Veyra is running. Fully exit the program before running this script,',
                    '  echo        otherwise exe, shaders and interface may only be partially replaced, leaving versions inconsistent.',
                    '  pause',
                    '  exit /b 1',
                    ')', '',
                    'rem --- Existence check (if one is missing, do nothing; avoid switching halfway) ---']
    for rel in files:
        L += ['if not exist "%s%s" (' % (rel, suffix),
              '  echo [Error] Missing: %s%s - cannot switch.' % (rel, suffix),
              '  pause',
              '  exit /b 1',
              ')']
    L += ['if not exist "%s\\Veyra" (' % src_dir,
          '  echo [Error] Missing folder: %s\\Veyra - cannot switch.' % src_dir,
          '  pause',
          '  exit /b 1',
          ')', '', 'rem --- Replace ---']
    for rel in files:
        L.append('copy /y "%s%s" "%s" >nul 2>nul' % (rel, suffix, rel))
    L += ['if errorlevel 1 (',
          '  echo [Error] File replacement failed; fully exit Veyra before running this script.',
          '  pause',
          '  exit /b 1',
          ')',
          'rem Whole-directory interface sync (%d files differ)' % len(qml_diff),
          'robocopy "%s\\Veyra" "qml\\Veyra" /MIR /NFL /NDL /NJH /NJS /NP >nul' % src_dir,
          'if errorlevel 8 (',
          '  echo [Error] Interface sync failed; fully exit Veyra before running this script.',
          '  pause',
          '  exit /b 1',
          ')', '']
    L.append('echo Switched to ' + ('P5 / HDR custom build.' if is_p5 else 'Author complete edition.'))
    if is_p5:
        L += ['echo   * Correct Dolby Vision Profile 5 conversion (no longer green-tinted)',
              'echo   * HDR luminance curve + seven-step presets + intensity + monitor peak + HDR10 metadata',
              "echo   * This repo's customizations, e.g. cleared landing page and build adaptation"]
    else:
        L += ['echo   Note: this edition has no Dolby Vision Profile 5 conversion; playing P5 sources will skew green;',
              'echo         and no HDR luminance curve customizations. For everyday use, prefer "launch-custom-build.cmd".']
    L += ['echo Starting Veyra ...',
          # Each version must use its own data directory: this repo extended the chain-session / preset-library fields and used new version numbers, so the upstream decoder cannot read our files and would report "session data damaged, refusing to overwrite". The data directory is the single root of all user state (sessions, UI preferences, preset library), so isolating it is enough.
          'start "" "veyra_qml_ui.exe" --data-dir "%%~dp0runtime_local\\user-data-2.0.0-%s" %%*'
          % ('p5' if is_p5 else 'author'),
          'exit /b 0', '']
    return '\n'.join(L)


def build_switch_doc(other, qml_diff, pkg_name: str) -> str:
    lines = [
        "Veyra two-version switch notes (%s + this repo's custom build)" % pkg_name,
        '=' * 50,
        '',
        'The two .cmd files switch between two versions on the same program. **This file is generated by tools/veyra-switch/make_switch.py from the actual differences between the author files and the custom build output**, so the files listed below are exactly the ones that will be replaced; every file not listed is identical in both versions and needs no switching (if a future upgrade makes shaders differ more, the script adds the corresponding rows automatically).',
        '',
        'Files that will be replaced:',
        '',
    ]
    for rel, a, b in other:
        lines.append('    %-34s  author %9d B / custom %9d B' % (rel, size(a), size(b)))
    lines.append('    %-34s %d files different (whole-directory sync)' % ('qml\\Veyra\\', len(qml_diff)))
    lines += [
        '',
        'Backup naming:',
        '',
        '    *.orig          author edition (veyra_qml_ui.exe.orig, each .dxil.orig)',
        "    *.p5            this repo's custom build (veyra_qml_ui.exe.p5, each .dxil.p5)",
        '    qml.orig\\Veyra   author edition interface',
        "    qml.p5\\Veyra     this repo's custom interface",
        '',
        '',
        "1) launch-custom-build.cmd      <- use this for everyday viewing",
        '   ------------------------------------------------',
        "   This repo's custom build, built from the author release source, includes:",
        '     * Dolby Vision Profile 5 converted correctly per IPT-PQ-C2 (no longer green-tinted)',
        '     * HDR luminance curve + seven one-click presets + intensity dial + monitor peak',
        '     * HDR10 static metadata (master peak / MaxCLL / MaxFALL)',
        '     * Other this-repo customizations, e.g. cleared landing page and build adaptation',
        '',
        '   Built from the locally available SDK: includes NVIDIA NR (DLSSNR), DLSS SR, DLSS FG, NVOF, VFG;',
        '   excludes Remote Play / Moonlight / Xbox / FSR / XeSS (their dependency sources are not available locally).',
        '   The PS5 / Xbox related dialogs show missing-property hints because the features were not compiled in; that is expected.',
        '',
        '   VFG (video interpolation) notes: a compile-time switch, needs CUDA driver headers',
        '   (CMake variable VEYRA_CUDA_DRIVER_INCLUDE_DIR, pointing at a directory containing cuda.h).',
        '   Measured advice: 2X-4X for the VFG multiplier (6X triggers many timeline resets); diagnostics frames must use material with an audio track.',
        '',
        '',
        "2) launch-author-edition.cmd    <- use when you need streaming features",
        '   ------------------------------------------------',
        '   Author release as-is, feature-complete (including PS5 Remote Play / Moonlight / Xbox streaming / FSR / XeSS).',
        "   Contains none of this repo's customizations: no P5 conversion (P5 sources skew green), no HDR luminance curve, etc.",
        '',
        '',
        '3) Relationship of the two (important: each version has its own data directory)',
        '   ------------------------------------------------',
        '   The same program directory, the same runtime (runtime\\  and runtime_local\\  shared, untouched by switching);',
        "   the two exes are built from the same author source baseline, differing in compile options and this repo's customizations.",
        '',
        '   Settings and sessions **are stored separately**; each script passes its own --data-dir:',
        '',
        "     this repo's custom build  runtime_local\\user-data-2.0.0-p5",
        "     author complete edition    runtime_local\\user-data-2.0.0-author",
        '',
        '   Why they must be separate: this repo extended the chain-session (mode-session) and preset-library **fields and used new version numbers**,',
        '   so the upstream decoder cannot read our files. If both shared one directory, switching to the author edition would read our session,',
        '   and the field misalignment would fail to parse; the author edition would then judge the file damaged and refuse to save, and the UI would show',
        '   "current changes not saved: session damaged, refusing to overwrite the original file". Storing them separately keeps them out of each other\'s way.',
        "   The cost: each version's settings are independent (different formats, no way to interoperate anyway).",
        '   Always launch with these two .cmd scripts - double-clicking veyra_qml_ui.exe directly would fall back to the shared default directory.',
        '',
        '',
        '4) When something goes wrong',
        '   ------------------------------------------------',
        '   * The script first checks whether the program is running; if so it prompts and exits without touching any file.',
        '     That avoids replacing only half (mismatched exe / shaders / interface). Fully exit Veyra before running.',
        '   * If it still reports "cannot write", end veyra_qml_ui.exe in the Task Manager and try again.',
        '   * To tell which version is active: look at the size of veyra_qml_ui.exe (custom build ~5.6 MB, author edition larger).',
        '   * To restore the author edition manually: copy each *.orig back to its original name, and overwrite qml\\Veyra with qml.orig\\Veyra.',
        '',
        '',
        "Build info is in the notes file alongside this one; the full customization and pitfalls record is in the source repository's CUSTOM_LEDGER.md.",
    ]
    return '\n'.join(lines) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--package', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--pkg-label', default='', help='Version label written into the notes file, e.g. "NVIDIA 2.0.4"')
    args = ap.parse_args()

    for label, p in (('package directory', args.package), ('build directory', args.build), ('source directory', args.repo)):
        if not os.path.isdir(p):
            print('Cannot find %s: %s' % (label, p))
            return 2

    live = os.path.join(args.package, 'veyra_qml_ui.exe')
    built = os.path.join(args.build, 'veyra_qml_ui.exe')
    if os.path.isfile(live) and os.path.isfile(built) and sha(live) == sha(built):
        print('Reminder: the exe in the package directory is already identical to the build output (it looks like the custom build).')
        print('      If that is not the state you expect, first restore the author edition with the *.orig files before running this tool.')
        print()

    other, qml_diff, notes = collect_diff(args.package, args.build, args.repo)
    print('=' * 78)
    print('Diff comparison result')
    print('=' * 78)
    for n in notes:
        print('  ' + n)
    print()
    print('  Files entering the switch set:')
    for rel, a, b in other:
        print('     %-38s  author %9d B / custom %9d B' % (rel, size(a), size(b)))
    print('     %-38s %d files different (whole-directory sync)' % ('qml\\Veyra\\', len(qml_diff)))
    for rel, a, b in qml_diff:
        print('        %-32s  author %7d / custom %7d' % (rel, size(a), size(b)))

    if not args.apply:
        print()
        print('(no --apply given; nothing changed)')
        return 0

    print()
    print('=' * 78)
    print('Backing up and generating scripts')
    print('=' * 78)
    for rel, a, b in other:
        if not (a and os.path.isfile(a) and os.path.isfile(b)):
            print('  * Skipped (missing file): %s' % rel)
            continue
        shutil.copy2(a, os.path.join(args.package, rel + '.orig'))
        shutil.copy2(b, os.path.join(args.package, rel + '.p5'))
        print('  %-38s -> .orig / .p5' % rel)

    qo = os.path.join(args.package, 'qml.orig', 'Veyra')
    qp = os.path.join(args.package, 'qml.p5', 'Veyra')
    for target, src in ((qo, os.path.join(args.package, 'qml', 'Veyra')),
                        (qp, os.path.join(args.repo, 'qml', 'Veyra'))):
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(src, target)
        print('  %-38s -> %d files' % (os.path.relpath(target, args.package), len(os.listdir(target))))

    write_utf8_crlf(os.path.join(args.package, 'launch-custom-build.cmd'), build_cmd(True, other, qml_diff))
    write_utf8_crlf(os.path.join(args.package, 'launch-author-edition.cmd'), build_cmd(False, other, qml_diff))
    write_utf8(os.path.join(args.package, 'switch-notes.txt'),
               build_switch_doc(other, qml_diff, args.pkg_label or 'author release'), bom=True)
    print('  Generated launch-custom-build.cmd / launch-author-edition.cmd / switch-notes.txt')

    for rel, _a, b in other:
        if os.path.isfile(b):
            shutil.copy2(b, os.path.join(args.package, rel))
    if os.path.isdir(qp):
        shutil.rmtree(os.path.join(args.package, 'qml', 'Veyra'))
        shutil.copytree(qp, os.path.join(args.package, 'qml', 'Veyra'))
    print('  Effective files set to the custom build')
    return 0


if __name__ == '__main__':
    sys.exit(main())
