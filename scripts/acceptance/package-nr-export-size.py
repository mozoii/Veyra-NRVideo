"""Seal local nre1 EXE/UI update. No runtime, model, user configuration or test media."""
from pathlib import Path
import json,subprocess,hashlib,zipfile,shutil
B=Path('E:/项目/Veyra');T='export-nr-scale-20261025';R=Path(__file__).resolve().parents[2];O=B/'reports'/T;D=B/'build/claude-screen-recording-20261022';P=B/'test-packages'/T/'Veyra-2.1.0-nre1-NVIDIA-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit the verified source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert all(x['exit']==0 for x in json.loads((O/'contracts.json').read_text('utf8')))
for name in ['gpu-exports-all.json','gpu-exports-4k-flow.json']:assert all(x['passed'] for x in json.loads((O/name).read_text('utf8')))
ui=json.loads((O/'ui-preset-export.json').read_text('utf8'));assert ui['passed']
assert all(x['exit']==0 for x in json.loads((O/'image-flow-probes.json').read_text('utf8')))
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(D/'veyra_qml_ui.exe')==ui['exeSha256']
assert not P.exists();P.mkdir(parents=True)
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(R/folder))
for name in ['EXPORT_NR_SCALE_PLAN_2026-10-25.md','EXPORT_NR_SCALE_REPORT_2026-10-25.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md']:copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
note='''# Veyra 2.1.0-nre1 NVIDIA local test update

Requires the existing complete 2.1.0 NVIDIA portable package. Close the player, copy this directory's files to the original package root (where veyra_qml_ui.exe lives), overwrite same-name files, then start.
This update contains only EXE, QML, translations and the note; no DLL/model/SDK/log/user config. The original package-manifest records the complete base package; after the update, the program/UI identity is authoritative in LOCAL_UPDATE.json.

- Video export NR internal processing size follows per-layer fixed tiers and manual 25%-100%, no longer forced native. 50% scales both width and height, about a quarter of the pixels; the final output size is still decided by the export settings.
- Example: 4K chooses 1080p or 50%, NR processes at 1920x1080 and composites back to 4K; 8K chooses 25%, NR processes at 1920x1080 and outputs 8K.
- Auto fixes a maximum of 1440p in the video-task snapshot, AMD keeps the 1080p budget; AMD manual ratio first respects the existing budget then scales. Images are still fully processed. Node video export still stays unsupported, use list presets.
- The export's selected preset is independent of the current preview parameters; changing the slider after the task freezes does not change the in-flight task. Lowering the internal size may change NR effects, cannot be equated to native-size picture quality.
- Includes the earlier NR manual slider, fg1 background VRR query, rotation and recording fixes. Lecram is paused. This round did not change frame-generation scheduling, NR models or runtime libraries.
- This machine RTX5070/SF runtime verified 4K/8K composite short-film real export, per-layer NR creation size, NVENC encoding, output size/frame count/non-black decode, UI-saved 37% preset actual export and full-size image GPU probe. AMD physical card, real 8K black-scene material, HDR, long-term picture quality not accepted this round; the 8K short-film export uses zero-motion input, with a separate 4K real NVOF export case.

Display version 2.1.0-nre1, PE numeric 2.1.0.0. Isolated branch local delivery, not merged to main, not pushed or published.
'''
(P/'README-NRE1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for e in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-nre1-local-update',base='Complete 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(R),localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
