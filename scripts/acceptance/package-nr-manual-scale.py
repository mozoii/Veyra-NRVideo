"""Seal a small local update: application/UI only, no runtime or user data."""
from pathlib import Path
import json,hashlib,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='nr-manual-scale-20261024';ROOT=Path(__file__).resolve().parents[2]
OUT=B/'test-packages'/T;REPORT=B/'reports'/T;BUILD=B/'build/claude-screen-recording-20261022'
head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain']).strip(),'commit the source before sealing'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
pkg=OUT/'Veyra-2.1.0-nr1-NVIDIA-update';assert not pkg.exists();pkg.mkdir(parents=True)
def copy(src,relative):
    dst=pkg/relative;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(BUILD/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (ROOT/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(ROOT/folder))
for name in ['NR_MANUAL_SCALE_PLAN_2026-10-24.md','NR_MANUAL_SCALE_REPORT_2026-10-24.md','FG_SCHEDULING_LOG_DIAGNOSIS_2026-10-24.md']:
    copy(ROOT/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(ROOT/name,name)
note='''# Veyra 2.1.0-nr1 local small update (NVIDIA)

This is an update layer, requiring the existing complete 2.1.0 NVIDIA portable package.
Close the player, copy this directory's files to the original package root (where veyra_qml_ui.exe lives), overwrite same-name files, then start.
This update contains and does not replace any DLL/model/runtime library, contains no user config. Do not place files into an extra nested directory and then directly launch the EXE.

- NR internal processing resolution gains "manual": 25%-100%, 1% step, applied on slider release; supports keyboard, input and reset.
- The ratio targets the layer input's width/height; 50% is about a quarter of the pixels, output size unchanged. Video preview only, image/video export contract unchanged.
- Shared by list/node, each layer independent, current manual ratio saved in the preset/session; the old version does not recognize the new manual tier, must use this update to read.
- Includes the branch's earlier recording, AMD super-resolution off and picture rotation fixes. Lecram remains paused.
- Frame-generation "scheduling demotion" this round only investigates, does not change scheduling code; detailed conclusion in docs/FG_SCHEDULING_LOG_DIAGNOSIS_2026-10-24.md.
- This machine RTX5070 passes 1080p/4K real NR inference, node, restart recovery and 110 QtQuick tests; AMD physical card/HDR/real-capture long-term run not accepted this round.

Internal version 2.1.0-nr1, PE numeric version still 2.1.0.0. LOCAL_UPDATE.json records the update layer and source commit; the original package-manifest.json only represents the original complete package, the updated EXE/UI is authoritative in LOCAL_UPDATE.json.
Source in the local Veyra worktree; this is a local test update, not merged to main, not pushed or published. GPL and third-party notices are kept with the update.
'''
(pkg/'README-NR1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(pkg).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(pkg.rglob('*')) if p.is_file()]
assert not any(Path(x['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil'] for x in files)
manifest=dict(version='2.1.0-nr1-local-update',base='Complete Veyra 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(ROOT),localOnly=True,releaseReady=False,containsRuntime=False,files=files)
(pkg/'LOCAL_UPDATE.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=1),'utf8')
archive=OUT/(pkg.name+'.zip');assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in pkg.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(pkg).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,package=str(pkg),zip=str(archive),bytes=archive.stat().st_size,sha256=sha(archive),exeSha256=sha(pkg/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(REPORT/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
