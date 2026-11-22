"""Finalize the locally tested NVIDIA repair; never alter the published package."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='rtss-restart-loop-20261004'
OUT=BASE/'test-packages'/TASK; LOGS=BASE/'logs'/TASK; TMP=BASE/'tmp'/TASK
APP=OUT/'Veyra-2.0.3-rtssfix-NVIDIA-win64-portable'
PUBLISHED=BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable'
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result);return result

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/rtss-restart-control.py')],check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),'Commit the reviewed local changes first'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
matrix=json.loads((LOGS/'installed-matrix-v3/summary.json').read_text(encoding='utf8'))
stress=json.loads((LOGS/'installed-stress-v4/summary.json').read_text(encoding='utf8'))
assert len(matrix['results'])==11 and len(stress['results'])==3
assert sum(len(r['cases']) for r in stress['results'])==33
assert json.loads((LOGS/'installed-stress-v4/cleanup.json').read_text(encoding='utf8'))['profilesRestored']
assert not json.loads((LOGS/'installed-matrix-v3/cleanup.json').read_text(encoding='utf8'))['remainingOwnedHelpers']
pkg=module('rtss_qml_package',ROOT/'scripts/package-qml-release.py')
vendor=module('rtss_vendor_package',ROOT/'scripts/package-vendor-release.py')
old=json.loads((PUBLISHED/'package-manifest.json').read_text(encoding='utf8'))
product_changes={'veyra_qml_ui.exe','qml/Veyra/Main.qml','qml/Veyra/SettingsPage.qml'}
doc_changes={'docs/'+n for n in ('RTSS_COMPAT_2026-10-03.md','FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md','RELEASE_2.0.3_ACCEPTANCE_2026-10-04.md')}
assert digest(APP/'veyra_qml_ui.exe')==digest(BASE/'build'/TASK/'veyra_qml_ui.exe')
for name in ('Main.qml','SettingsPage.qml'):
    assert (APP/'qml/Veyra'/name).read_bytes()==(ROOT/'qml/Veyra'/name).read_bytes(),'Acceptance loader remains'
for name in doc_changes:
    if (APP/name).exists():shutil.copy2(ROOT/name,APP/name)
shutil.copy2(ROOT/'docs/RTSS_RESTART_LOOP_PLAN_2026-10-04.md',APP/'docs/RTSS_RESTART_LOOP_PLAN_2026-10-04.md')
(APP/'RTSS-LOCAL-FIX.md').write_text('''# Veyra 2.0.3 RTSS local fix build (NVIDIA)

Just run veyra_qml_ui.exe in this directory; components are complete. Default monitoring-software compatibility is automatic.

Fix: orphaned RTSS loader or leftover shared memory no longer runs as an RTSS overlay service;
clicking compatibility restart carries this launch intent, avoiding the same prompt reappearing after restart.
The prompt distinguishes RTSS from MSI Afterburner, software-drawn background stays opaque.

RTX 5070 / 616.56, the user's actual installed RTSS 7.3.5.28314:
11 launch/restart plus 3 RTSS configurations x 11 real playback/effect/window combinations pass.
Including NR parameters, highest RTX 4K super-resolution, DLSS 2X/6X, XeSS 4X, FSR3.1 2X,
VFG 2X Low/4X Medium/8X High, pause/seek/fullscreen/window restore.
OSD 2/3x, position, color, background, detection level, OSD switch and 60-frame limit actually set and read back.
This is a short test; VFG 8X High may be scheduled downgraded in 2K30 real-time playback, no 240 fps guarantee.

RTSS test configuration restored; the test main process and loader have exited.
This package does not contain user config, test scripts, media or logs; runtime component bytes are the same as the official NVIDIA 2.0.3 package.
The local fix is not yet merged into main or uploaded to GitHub; the original official package is unchanged.
The corresponding Veyra source ZIP is in this directory's parent; unchanged dependency source is reused from the official 2.0.3 assets.
''',encoding='utf8')
changed=[]
for row in old['files']:
    assert (PUBLISHED/row['path']).stat().st_size==row['size'] and digest(PUBLISHED/row['path'])==row['sha256'],('Published payload changed',row['path'])
    if digest(APP/row['path'])!=row['sha256']:changed.append(row['path'])
assert set(changed)<=product_changes|doc_changes,changed
assert product_changes<=set(changed),changed
names=vendor.check_vendor(APP,'NVIDIA')
for name in names:
    pkg.validate_payload(name)
    if name.endswith('.qml'):
        assert 'scripts/acceptance/rtss-restart-' not in (APP/name).read_text(encoding='utf8',errors='ignore')
rows=[dict(path=f.relative_to(APP).as_posix(),size=f.stat().st_size,sha256=digest(f))
      for f in sorted(APP.rglob('*')) if f.is_file() and f.name!='package-manifest.json']
source_name='Veyra-2.0.3-rtssfix-veyra-source.zip'
manifest={k:v for k,v in old.items() if k!='files'}
manifest.update(candidate='2.0.3-rtssfix',displayVersion='2.0.3-rtssfix',baseCommit=commit,sourceArchiveCommit=commit,
                worktreeDirty=False,releaseReady=False,localOnly=True,
                publishedBase='f82f6499ff0db9c36953bcafb752b9be2d7fca4d',
                correspondingSource=dict(application='../'+source_name,dependencies=old['correspondingSource']['dependencies'],
                                         instructions='docs/BUILD_2.0.3.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.3-rtssfix'),files=rows)
write(APP/'package-manifest.json',manifest)
audit=TMP/'package-audit.py'
original=(ROOT/'scripts/acceptance/release-2.0.3-package-audit.py').read_text(encoding='utf8')
assert original.count("TASK='release-2.0.3-20261004'")==1
audit.write_text(original.replace("TASK='release-2.0.3-20261004'","TASK='"+TASK+"'"),encoding='utf8')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP))
subprocess.run([sys.executable,'-B',str(audit),str(APP),'NVIDIA','local-package-audit'],env=env,check=True,timeout=150)
pkg.source_snapshot(OUT,'2.0.3-rtssfix',digest(APP/'veyra_qml_ui.exe'))
source=OUT/source_name
patch=subprocess.check_output(['git','diff','--binary','354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff',commit],cwd=ROOT)
(BASE/'archives'/TASK/'repair-source.patch').write_bytes(patch)
write(OUT/'DELIVERY.json',dict(candidate='2.0.3-rtssfix',app=str(APP/'veyra_qml_ui.exe'),appSHA256=digest(APP/'veyra_qml_ui.exe'),
      sourceCommit=commit,source=str(source),sourceSHA256=digest(source),packageManifestSHA256=digest(APP/'package-manifest.json'),
      payloadFiles=len(rows),changedPublishedPayload=changed,localOnly=True,releaseReady=False,
      startupRestartCases=11,rtssParameterCases=33,logs=str(LOGS),
      limitation='Short RTX5070/616.56 + RTSS7.3.5.28314 test; VFG8 High realtime scheduling reduction observed; no new AMD/OBS/Xbox/long-run validation'))
print('RTSS LOCAL DELIVERY PASS',commit,len(rows),digest(APP/'veyra_qml_ui.exe'),OUT,flush=True)
