"""Seal verified local scheduling update; no runtimes, logs or user configuration."""
from pathlib import Path
import json,hashlib,subprocess,zipfile,shutil
B=Path('E:/项目/Veyra');T='fg-scheduling-repair-20261024';R=Path(__file__).resolve().parents[2];O=B/'reports'/T
D=B/'build/claude-screen-recording-20261022';P=B/'test-packages'/T/'Veyra-2.1.0-fg1-NVIDIA-update'
assert not subprocess.check_output(['git','-C',str(R),'status','--porcelain']).strip(),'commit reviewed source first'
head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()
assert json.loads((O/'native.json').read_text('utf8'))['passed']
comparisons=json.loads((O/'capture-comparison.json').read_text('utf8'));assert len(comparisons)==4 and all(x['actualFormat'].startswith('2560x1440p60 NV12') for x in comparisons)
assert all(x['exit']==0 for x in json.loads((O/'contracts.json').read_text('utf8')))
assert not P.exists();P.mkdir(parents=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def copy(src,rel):
    dst=P/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
copy(D/'veyra_qml_ui.exe','veyra_qml_ui.exe')
assert sha(P/'veyra_qml_ui.exe')==json.loads((O/'native.json').read_text('utf8'))['exeSha256']
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():copy(p,Path(folder)/p.relative_to(R/folder))
for name in ['FG_SCHEDULING_REPAIR_PLAN_2026-10-24.md','FG_SCHEDULING_REPAIR_REPORT_2026-10-24.md','NR_MANUAL_SCALE_REPORT_2026-10-24.md']:
    copy(R/'docs'/name,Path('docs')/name)
for name in ['LICENSE','THIRD_PARTY_NOTICES.md']:copy(R/name,name)
note='''# Veyra 2.1.0-fg1 NVIDIA local test update

Requires the existing complete 2.1.0 NVIDIA portable package. First close the player, copy this directory's files to the original package root (where veyra_qml_ui.exe lives), overwrite same-name files, then start.
This update contains only program, QML, translations and the note, no DLL/model/SDK/log/user config. The original package-manifest describes the complete base package; this update's program/UI identity is authoritative in LOCAL_UPDATE.json.

- Move display refresh rate and NVIDIA G-SYNC/VRR status queries to the background, avoiding that a synchronous driver query every two seconds causes the current captured frame to miss the frame-gen deadline. The playback thread only takes the completed result.
- Keep the original frame-gen budget, deadline, skipped-frame and warning determinations, no frame backlog, and does not guarantee frame-gen when the load truly exceeds the budget.
- Also keep the per-layer NR manual 25%-100% internal ratio slider added in nr1, and the earlier rotation/recording fixes; Lecram remains paused.
- This machine KUHAIMI G50 real capture 2560x1440p60 NV12, NR/SR off, VFG2X: old nr1 vs fg1 comparison, including the original 60 and 60->30 two groups; detailed counts in the accompanying repair report.
- Verified 4K30 file VFG2X/4X, follow-display/VRR, pause recovery, close-reopen and targeted CPU regression. Other capture cards/other FG backends/heavy NR load/HDR/long-term and optical latency are not proven by this short test.

Version identifier 2.1.0-fg1, PE numeric version 2.1.0.0. Local isolated-branch delivery, not merged to main, not pushed or published.
'''
(P/'README-FG1.zh-CN.md').write_text(note,'utf8')
files=[dict(path=p.relative_to(P).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(P.rglob('*')) if p.is_file()]
assert not any(Path(x['path']).suffix.lower() in ['.dll','.onnx','.tflite','.lib','.pdb','.hsaco','.dxil','.log'] for x in files)
(P/'LOCAL_UPDATE.json').write_text(json.dumps(dict(version='2.1.0-fg1-local-update',base='Complete 2.1.0 NVIDIA portable package',sourceCommit=head,sourceWorktree=str(R),localOnly=True,releaseReady=False,containsRuntime=False,files=files),ensure_ascii=False,indent=1),'utf8')
zpath=P.parent/(P.name+'.zip');assert not zpath.exists()
with zipfile.ZipFile(zpath,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in P.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(zpath) as z:
    assert z.testzip() is None
    for e in files:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result=dict(sourceCommit=head,zip=str(zpath),bytes=zpath.stat().st_size,sha256=sha(zpath),exeSha256=sha(P/'veyra_qml_ui.exe'),manifestFiles=len(files),runtimeFiles=0)
(O/'update-package.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
