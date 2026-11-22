"""Local rotation packages. Never overwrites an existing package or donor link.

usage: python -X utf8 -B -I scripts/acceptance/package-picture-rotation.py
"""
from pathlib import Path
import hashlib,json,os,subprocess,zipfile

B=Path('E:/项目/Veyra'); T='codex-picture-rotation-20261023'
ROOT=Path(__file__).resolve().parents[2];BUILD=B/'build/claude-screen-recording-20261022'
OUT=B/'test-packages'/T;REPORT=B/'reports'/T
for p in [OUT,REPORT]:p.mkdir(parents=True,exist_ok=True)
head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip(),'commit source before sealing'

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def replace(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():p.unlink()
    p.write_bytes(data)

note='''# Veyra 2.1.0 picture-rotation local test build (rot1)

  Full extraction, run veyra_qml_ui.exe. The build reuses the internal 2.1.0-rec identifier; the feature is authoritative in this file and the rot1 manifest.

- Minimal player bar, professional player bar and fullscreen player bar each have a clockwise rotation small button, 90 degrees each, 4 to restore.
- The minimal window adjusts aspect ratio; the professional/fullscreen picture adapts to the preview area, controls stay upright.
- Existing video orientation info and manual rotation are stacked. Rotation resets zoom/pan and cancels unfinished protective areas.
- Switching mode, pause, seek, enter/exit fullscreen keep the angle; a new source resets it.
- This feature changes the playback view; screenshots, recording and export still use the original orientation of the processed texture, not rewriting the media.
- Game-mode rotation follows the non-in-place preview strategy: keeps keyboard/controller, blocks misaligned mouse pass-through.
- Includes last round AMD super-resolution-off and recording-fix; proprietary runtime/model/compiled shaders unchanged.

  Local verification scope and unverified items: docs/PICTURE_ROTATION_REPORT_2026-10-23.md. Do not claim no bugs.
  Not merged to main or published, Lecram paused per the user's request.
'''
results=[]
for vendor in ['NVIDIA','AMD']:
    donor=B/'releases/release-2.1.0-20261019/runtime-base'/vendor
    pkg=OUT/f'Veyra-2.1.0-rot1-{vendor}-win64-portable'
    assert donor.is_dir() and not pkg.exists(),pkg
    for src in donor.rglob('*'):
        dst=pkg/src.relative_to(donor)
        if src.is_dir():dst.mkdir(parents=True,exist_ok=True)
        else:os.link(src,dst)
    replace(pkg/'veyra_qml_ui.exe',(BUILD/'veyra_qml_ui.exe').read_bytes())
    for folder in ['qml','i18n']:
        for src in (ROOT/folder).rglob('*'):
            if src.is_file():replace(pkg/folder/src.relative_to(ROOT/folder),src.read_bytes())
    for name in ['CLAUDE_HANDOFF_PLAN_2026-10-22.md','CLAUDE_HANDOFF_REPORT_2026-10-22.md','PICTURE_ROTATION_PLAN_2026-10-23.md','PICTURE_ROTATION_REPORT_2026-10-23.md']:
        replace(pkg/'docs'/name,(ROOT/'docs'/name).read_bytes())
    replace(pkg/'README-TEST.zh-CN.md',note.encode('utf8'))
    old=json.loads((donor/'package-manifest.json').read_text('utf8'));unchanged=[]
    for e in old['files']:
        if e['path'].lower().endswith(('.dll','.onnx','.tflite','.dxil','.hsaco')):
            assert sha(pkg/e['path'])==e['sha256'].lower()==sha(donor/e['path']),e['path']
            unchanged.append(e['path'])
    files=[dict(path=p.relative_to(pkg).as_posix(),size=p.stat().st_size,sha256=sha(p)) for p in sorted(pkg.rglob('*')) if p.is_file() and p.name!='package-manifest.json']
    manifest=dict(old);manifest.update(candidate='2.1.0-rot1-local',sourceArchiveCommit=head,productSourceCommit=head,sourceParent='4eee190617615bbd55fea2fc84d171ec14639df1',validationRecord='docs/PICTURE_ROTATION_REPORT_2026-10-23.md',exeSha256=sha(pkg/'veyra_qml_ui.exe'),files=files,localOnly=True,releaseReady=False)
    replace(pkg/'package-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2).encode('utf8'))
    archive=Path(str(pkg)+'.zip');assert not archive.exists(),archive
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
        for p in pkg.rglob('*'):
            if p.is_file():z.write(p,pkg.name+'/'+p.relative_to(pkg).as_posix())
    with zipfile.ZipFile(archive) as z:assert z.testzip() is None
    result=dict(vendor=vendor,package=str(pkg),zip=str(archive),source=head,exeSha256=manifest['exeSha256'],zipSha256=sha(archive),zipBytes=archive.stat().st_size,manifestFiles=len(files),unchangedRuntimeModelShaderFiles=len(unchanged))
    results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
(REPORT/'packages.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
