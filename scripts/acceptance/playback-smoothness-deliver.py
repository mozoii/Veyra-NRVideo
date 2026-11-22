"""Create an isolated complete NVIDIA candidate from verified manifest files."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004';LABEL='2.0.3-smoothfix'
OUT=BASE/'test-packages'/TASK;LOGS=BASE/'logs'/TASK;TMP=BASE/'tmp'/TASK
APP=OUT/('Veyra-'+LABEL+'-NVIDIA-win64-portable')
PUBLISHED=BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable'

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/playback-smoothness-control.py')],cwd=ROOT,check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit reviewed source before packaging'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
assert all(r['passed'] for r in json.loads((LOGS/'native-tests.json').read_text(encoding='utf8')))
for test in ('functional-v5-final','layout-fullscreen-v4-final','capability-v1','candidate-software-v2-final','candidate-vfg4-v2-final'):
    assert json.loads((BASE/'tests'/TASK/test/'result.json').read_text(encoding='utf8'))['passed'],test
pkg=module('smooth_qml_package',ROOT/'scripts/package-qml-release.py')
vendor=module('smooth_vendor_package',ROOT/'scripts/package-vendor-release.py')
old=json.loads((PUBLISHED/'package-manifest.json').read_text(encoding='utf8'))
OUT.mkdir(parents=True,exist_ok=False);APP.mkdir()
for row in old['files']:
    src=PUBLISHED/row['path'];dst=APP/row['path']
    assert src.stat().st_size==row['size'] and digest(src)==row['sha256'],('Published identity',row['path'])
    pkg.validate_payload(row['path']);dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(src,dst)
shutil.copyfile(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml/Veyra',APP/'qml/Veyra',dirs_exist_ok=True)
docs=('PLAYBACK_SMOOTHNESS_PLAN_2026-10-04.md','SUBTITLE_ENGINE_2026-09-16.md',
      'RTSS_RESTART_LOOP_PLAN_2026-10-04.md','RTSS_COMPAT_2026-10-03.md')
for name in docs:
    dst=APP/'docs'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'docs'/name,dst)
(APP/'LOCAL-FIX.md').write_text('''# Veyra 2.0.3-smoothfix local test package (NVIDIA)

Run this directory's veyra_qml_ui.exe; base dependencies and already-audited runtime components are complete.

- Custom file playback speed 0.25x-4.00x, supporting two decimals, keeping the original pitch.
- Fixes: embedded subtitle body comma eaten, two-line/three-line subtitles missing, and VTT CRLF.
- Capability queries no longer hit the disk/rebuild menus repeatedly with each playback snapshot; replacement component discovery and gray entries are kept.
- Professional mode keeps only the input info at the top, with output resolution next to it; narrow windows wrap progress and buttons.
- Fixes: playback-speed menu clipping/tip overlap; custom windows accept keyboard input.
- Window maximized and then exited fullscreen restores maximized state; normal windows restore their original size.
- Includes the earlier local RTSS false-positive and compatibility restart-loop fixes.

RTX5070 / 616.56 short test: audio 18 groups, subtitle 30 author cues, hardware capability 56 groups,
real Qt playback/custom speed/subtitle/menu/720-1600 width/fullscreen restore all pass.
Software UI background DLSS6X submission ~180fps, UI timing sample median 16ms; VFG4X submission median ~115fps.
These are software submission/timing metrics, cannot be taken as actual screen refresh rate or end-to-end latency.
The user-reported severe background dropped frames did not reproduce; no root cause was found; display ETW capture was rejected by local permissions.
Added focus/throughput/system-process throttling logs can help further location. No new AMD physical-card acceptance.

This package was not merged into main, not uploaded or replaced GitHub 2.0.3; kept separate from the original official package.
Runtime bytes, license and GPU split rules kept; does not include AMD NR, SDK, test media, personal config or logs.
The source ZIP is in the parent directory, the original dependency source is reused from the published 2.0.3 assets. Full evidence in the local logs task directory.
''',encoding='utf8')
names=vendor.check_vendor(APP,'NVIDIA')
for name in names:
    pkg.validate_payload(name)
    if name.startswith('qml/Veyra/') and name.endswith('.qml'):
        assert 'scripts/acceptance/' not in (APP/name).read_text(encoding='utf8',errors='ignore'),'Acceptance loader in product'
changed=[r['path'] for r in old['files'] if digest(APP/r['path'])!=r['sha256']]
allowed={'veyra_qml_ui.exe','docs/RTSS_COMPAT_2026-10-03.md'}
allowed.update(p for p in subprocess.check_output(['git','diff','--name-only','f82f6499ff0db9c36953bcafb752b9be2d7fca4d',commit],cwd=ROOT).decode().splitlines() if p.startswith('qml/Veyra/'))
assert set(changed)<=allowed,changed
rows=[dict(path=f.relative_to(APP).as_posix(),size=f.stat().st_size,sha256=digest(f)) for f in sorted(APP.rglob('*')) if f.is_file() and f.name!='package-manifest.json']
source_name='Veyra-'+LABEL+'-veyra-source.zip'
manifest={k:v for k,v in old.items() if k!='files'}
manifest.update(candidate=LABEL,displayVersion=LABEL,baseCommit=commit,sourceArchiveCommit=commit,
    worktreeDirty=False,releaseReady=False,localOnly=True,publishedBase='f82f6499ff0db9c36953bcafb752b9be2d7fca4d',
    correspondingSource=dict(application='../'+source_name,dependencies=old['correspondingSource']['dependencies'],
                             instructions='docs/BUILD_2.0.3.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION='+LABEL),files=rows)
write(APP/'package-manifest.json',manifest)
audit=TMP/'smooth-package-audit.py'
original=(ROOT/'scripts/acceptance/release-2.0.3-package-audit.py').read_text(encoding='utf8')
assert original.count("TASK='release-2.0.3-20261004'")==1
audit.write_text(original.replace("TASK='release-2.0.3-20261004'","TASK='"+TASK+"'"),encoding='utf8')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP))
subprocess.run([sys.executable,'-B',str(audit),str(APP),'NVIDIA','local-package-audit'],env=env,check=True,timeout=150)
source=pkg.source_snapshot(OUT,LABEL,digest(APP/'veyra_qml_ui.exe'))
patch=subprocess.check_output(['git','diff','--binary','354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff',commit],cwd=ROOT)
(BASE/'archives'/TASK/'repair-source.patch').write_bytes(patch)
write(OUT/'DELIVERY.json',dict(candidate=LABEL,app=str(APP/'veyra_qml_ui.exe'),appSHA256=digest(APP/'veyra_qml_ui.exe'),
      sourceCommit=commit,source=str(OUT/source_name),sourceSHA256=source['sha256'],
      packageManifestSHA256=digest(APP/'package-manifest.json'),payloadFiles=len(rows),
      changedPublishedPayload=changed,localOnly=True,releaseReady=False,
      backgroundRootCauseFound=False,logs=str(LOGS),
      limitation='RTX5070/616.56 short tests; severe background drops not reproduced; ETW display events unavailable without privileges; AMD real hardware not tested'))
print('PLAYBACK LOCAL DELIVERY PASS',commit,len(rows),OUT,flush=True)
