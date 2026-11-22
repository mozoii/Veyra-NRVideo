"""Stage local handoff packages, audit unchanged donor runtimes and optionally ZIP.

No existing output is overwritten. All replacements unlink hard links first.
usage: python -X utf8 -B scripts/acceptance/package-recording-handoff.py [--zip]
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time, zipfile

BASE = Path('E:/项目/Veyra')
TASK = 'codex-claude-handoff-20261022'
ROOT = Path(__file__).resolve().parents[2]
BUILD = BASE / 'build/claude-screen-recording-20261022'
OUT = BASE / 'test-packages' / TASK
REPORT = BASE / 'reports' / TASK
parser = argparse.ArgumentParser()
parser.add_argument('--zip', action='store_true')
parser.add_argument('--seal-existing', action='store_true', help='Reuse this task staging after a failed sealing step')
args = parser.parse_args()
OUT.mkdir(parents=True, exist_ok=True)
REPORT.mkdir(parents=True, exist_ok=True)
head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def replace(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    path.write_bytes(data)

note = '''# Veyra 2.1.0 recording fix test build (rec2)

After complete extraction run veyra_qml_ui.exe. The software still shows 2.1.0-rec; this is a local test build.

- AMD super-resolution off fix already included.
- Professional mode screenshot-adjacent record button: 3-second countdown, cancellable; recording shows elapsed time, click to stop.
- Settings -> Playback -> Recording: save location, auto/specified bitrate, frame rate, H.264/HEVC, record sound.
- Records the post-processed picture and the player's sound; player volume/mute does not affect the recording; capture card needs audio monitoring enabled.
- Continue recording across effect changes; on output size or HDR mode change, keeps writing new files -2, -3, etc.
- Pause retains the real time gap; player stop or source change saves the recording.
- XeSS plugin-internal generated frames do not live in the engine texture pool; cannot claim to have recorded those generated frames.
- NVIDIA recording uses D3D12 NVENC. AMD/Intel uses the system hardware encoder, pixel readback and encoding on the recording worker thread; does not call this backend zero-copy.
- HDR recording is NVIDIA HEVC 10bit only; no HDR recording acceptance this round.

This machine passes: AMD integrated-GPU super-resolution toggle, 1080p60 with-sound recording, ~62s continuous recording, 4x frame-gen, FSR resolution switching, pause recovery, 4K file recording, H.264/HEVC, frame-limit/mute/no-audio-track, repeated start-stop and bad-directory handling.
See docs/CLAUDE_HANDOFF_REPORT_2026-10-22.md. No long-term stability, real capture card/host stream, other GPUs or screen optical latency acceptance.
In the isolated environment, switching to DLSS SR has the CreateFeature-hang problem, which also appears without a recording control; recorded separately, cannot claim it fixed.
Lecram is paused per the user's request, not included in this package. Not merged to main, not pushed or published.
'''
receipts = []
for vendor in ('NVIDIA', 'AMD'):
    donor = BASE / 'releases/release-2.1.0-20261019/runtime-base' / vendor
    package = OUT / f'Veyra-2.1.0-rec2-{vendor}-win64-portable'
    assert donor.is_dir(), donor
    if package.exists():
        assert args.seal_existing, package
        staged = json.loads((package/'package-manifest.json').read_text('utf8'))
        assert staged['candidate']=='2.1.0-rec2-local' and staged['exeSha256']==sha(BUILD/'veyra_qml_ui.exe')
    else:
        for source in donor.rglob('*'):
            target = package / source.relative_to(donor)
            if source.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            elif source.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                os.link(source, target)
    replace(package / 'veyra_qml_ui.exe', (BUILD / 'veyra_qml_ui.exe').read_bytes())
    for folder in ('qml', 'i18n'):
        for source in (ROOT / folder).rglob('*'):
            if source.is_file():
                target = package / folder / source.relative_to(ROOT / folder)
                if not target.exists() or target.read_bytes() != source.read_bytes():
                    replace(target, source.read_bytes())
    for name in ('CLAUDE_HANDOFF_PLAN_2026-10-22.md', 'CLAUDE_HANDOFF_REPORT_2026-10-22.md'):
        replace(package / 'docs' / name, (ROOT / 'docs' / name).read_bytes())
    replace(package / 'README-TEST.zh-CN.md', note.encode('utf8'))
    # Every publisher-supplied runtime, model and shader must stay byte-identical.
    original = json.loads((donor / 'package-manifest.json').read_text('utf8'))
    unchanged = []
    for entry in original['files']:
        rel = entry['path']
        if rel.lower().endswith(('.dll', '.onnx', '.tflite', '.dxil', '.hsaco')):
            actual = sha(package / rel)
            assert actual == entry['sha256'].lower() == sha(donor / rel), rel
            unchanged.append(rel)
    files = [{'path': p.relative_to(package).as_posix(), 'size': p.stat().st_size, 'sha256': sha(p)}
             for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'package-manifest.json']
    manifest = dict(original)
    manifest.update(candidate='2.1.0-rec2-local', displayVersion='2.1.0-rec', localOnly=True,
                    releaseReady=False, sourceArchiveCommit=head, mainCommit=subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "main"], text=True).strip(), productSourceCommit=head,
                    validationRecord='docs/CLAUDE_HANDOFF_REPORT_2026-10-22.md',
                    exeSha256=sha(package / 'veyra_qml_ui.exe'), files=files)
    replace(package / 'package-manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf8'))
    result = dict(vendor=vendor, package=str(package), source=head, exeSha256=manifest['exeSha256'],
                  files=len(files), unchangedRuntimeModelShaderFiles=len(unchanged))
    if args.zip:
        # Version dots are part of the directory name, not a file suffix.
        archive = Path(str(package) + '.zip')
        assert not archive.exists(), archive
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as z:
            for p in package.rglob('*'):
                if p.is_file():
                    z.write(p, package.name + '/' + p.relative_to(package).as_posix())
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
        result.update(zip=str(archive), zipBytes=archive.stat().st_size, zipSha256=sha(archive))
    receipts.append(result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
(REPORT / 'packages.json').write_text(json.dumps(receipts, ensure_ascii=False, indent=2), 'utf8')
