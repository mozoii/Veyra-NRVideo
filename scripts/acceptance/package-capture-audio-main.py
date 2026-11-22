"""Seal the common EXE/QML update without any runtime/model/configuration files."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='capture-audio-main-20261025';R=Path(__file__).resolve().parents[2];O=B/'reports'/T;D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';P=B/'test-packages'/T/'Veyra-2.1.0-int1-NVIDIA-AMD-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit the reviewed source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert subprocess.check_output(['git','-C',str(R),'rev-parse','main'],text=True).strip()==head,'merge local main first'
audit=json.loads((O/'integration-audit.json').read_text('utf8'));assert audit['sourceCommit']==head and audit['mainCommit']==head and audit['testsPassed']
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(D/'veyra_qml_ui.exe')==sha(A/'veyra_qml_ui.exe')==json.loads((O/'prepared.json').read_text('utf8'))['exeSha256']
# A failed seal may leave a partial directory. Resume only our known inputs;
# never overwrite a sealed package or accept unrelated files in that directory.
if P.exists():
    assert not (P/'LOCAL_UPDATE.json').exists(),'sealed package already exists'
    for p in P.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(P);expected=D/rel if rel.as_posix()=='veyra_qml_ui.exe' else R/rel
        assert expected.is_file() and p.read_bytes()==expected.read_bytes(),p
else:P.mkdir(parents=True)
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():
            tested=A/folder/p.relative_to(R/folder)
            # Switching the tested branch to main can materialize Git's CRLF
            # checkout policy. Compare content, then ship the exact tested bytes.
            assert tested.read_bytes().replace(b'\r\n',b'\n')==p.read_bytes().replace(b'\r\n',b'\n'),p
            copy(tested,Path(folder)/p.relative_to(R/folder))
for name in ['CAPTURE_AUDIO_MAIN_REPORT_2026-10-25.md','EXPORT_NR_SCALE_REPORT_2026-10-25.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md']:copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
(P/'README-INT1.en.md').write_text('''# Veyra 2.1.0-int1 local integration update

Requires the existing complete 2.1.0 NVIDIA or AMD portable package. First close the player, then copy this directory's contents to the original package root (where veyra_qml_ui.exe lives), overwriting same-name files. Keeping the original package is enough for fallback. This update contains only the common program/UI/readme, no DLL, model, driver, user config or log, and is not a complete portable package. NVIDIA and AMD share the same EXE; each original package keeps its own runtime components; the original package-manifest only represents the base package, update program identity is in LOCAL_UPDATE.json.

Includes: recording/AMD super-resolution off, 90-degree rotation, NR 25%-100% manual ratio, background VRR query, NR video export fixed/percentage size, #67 NR-close lifecycle, fullscreen button/Esc/playlist key ownership, capture with untimed-stem samples received normally and diagnostics.

The CV710 fix addresses the confirmed receive error: untimed samples no longer return failure to the driver and do not fabricate PTS. There is no CV710 original-card re-test, so this cannot be guaranteed to be the original-card root cause. If still stuck, please keep the new log; capture-video-delivery/time distinguishes driver-not-sending-video from sample-receive problems. The original 3090-close issue also needs original-machine re-test. Lecram remains paused.

This round passed Windows build, 9 targeted targets, 115 QtQuick items, real 5070 NR switch/open-state close/reopen, 4K37%+NVOF six-frame export, software/GPU interface native fullscreen operations. This machine G50 video and 48k audio only short-receive, does not represent CV710 or stable 60fps/long-term acceptance. AMD new-NR physical card/HDR/long-term not re-tested, original AMD SR-off/recording verification reuses the corresponding commit evidence. See docs integration report.

Only local main integration, no push/Release. Display version 2.1.0-int1, not changing the official release version.
''','utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for e in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-int1-local-update',base='Complete 2.1.0 NVIDIA or AMD portable package',sourceCommit=head,localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
row=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(row,ensure_ascii=False,indent=1),'utf8');print(json.dumps(row,ensure_ascii=False))
