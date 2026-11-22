"""One shared private app and a small overlay; keep runtimes unchanged."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='blackmagic-followup-20261009'
DONOR=BASE/'test-packages/vfg-next-20261007/Veyra-2.0.6-VFG-next-NVIDIA-win64-portable'
OUT=BASE/'test-packages'/TASK;APP=BASE/'tests'/TASK/'app';BUILD=BASE/'build'/TASK
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build():return json.loads(Path(json.loads((BASE/'reports'/TASK/'build-current.json').read_text(encoding='utf8'))['receipt']).read_text(encoding='utf8'))
def auditDonor():
 manifest=json.loads((DONOR/'package-manifest.json').read_text(encoding='utf-8-sig'))
 for row in manifest['files']:assert sha(DONOR/row['path'])==row['sha256'].lower(),row['path']
 return manifest
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/bmd-followup-control.py')],check=True)
receipt=build()
for n,d in receipt['sources'].items():assert sha(ROOT/n)==d,n
for n,d in receipt['executables'].items():assert sha(Path(n))==d['sha256'],n
manifest=auditDonor();OUT.mkdir(parents=True,exist_ok=True)
if sys.argv[1]=='prepare':
 assert not APP.exists();APP.parent.mkdir(parents=True,exist_ok=True)
 def linked(a,b):
  if Path(a).relative_to(DONOR).as_posix() in ('veyra_qml_ui.exe','qml/Veyra/Main.qml','THIRD_PARTY_NOTICES.md','package-manifest.json'):return shutil.copy2(a,b)
  os.link(a,b);return b
 shutil.copytree(DONOR,APP,copy_function=linked)
 shutil.copy2(BUILD/'veyra-bmd-followup.exe',APP/'veyra_qml_ui.exe');shutil.copy2(ROOT/'THIRD_PARTY_NOTICES.md',APP/'THIRD_PARTY_NOTICES.md')
 (BASE/'reports'/TASK/'app.json').write_text(json.dumps({'path':str(APP),'exeSha256':sha(APP/'veyra_qml_ui.exe'),'donorFilesVerified':len(manifest['files']),'sharedDependenciesUnmodified':True},ensure_ascii=False,indent=2),encoding='utf8')
 print('Prepared one private app; verified donor files',len(manifest['files']));raise SystemExit(0)
assert sys.argv[1]=='package'
assert sha(APP/'veyra_qml_ui.exe')==sha(BUILD/'veyra-bmd-followup.exe')
payload={'veyra_qml_ui.exe':BUILD/'veyra-bmd-followup.exe','THIRD_PARTY_NOTICES.md':ROOT/'THIRD_PARTY_NOTICES.md','Start-BMD-Capture-Test.ps1':ROOT/'scripts/diagnostics/Start-BMD-Capture-Test.ps1','licenses/capture/LIBDSHOWCAPTURE_LGPL.txt':ROOT/'licenses/capture/LIBDSHOWCAPTURE_LGPL.txt'}
# Use the complete matching QML/shader set; no new DLL/model/SDK is distributed.
for folder in ('qml/Veyra','shaders'):
 for p in (DONOR/folder).rglob('*'):
  if p.is_file():payload[p.relative_to(DONOR).as_posix()]=p
readme='''BMD / Blackmagic / DeckLink 2.0.6 compatibility test patch (not an official release)

1. Keep the original package; copy/extract a complete Veyra 2.0.6 portable package separately; unpack this patch over the copy root. AMD/NVIDIA keep their own original package runtime libraries.
2. Close OBS and other capture programs, run veyra_qml_ui.exe. Select the original BMD entry and use exactly the same resolution, frame rate and pixel format as when OBS shows a picture; 59.94 and 60, 1080p30 and 1080i60 are not interchangeable. Disable enhancement first to confirm the basic picture, then turn on the usual enhancement. The two names do not prove two physical cards.
3. The new compatibility mode defaults to auto-applied for BMD. Please test both entries and reconnect after disconnect. Do not use an arbitrary mode in place of the actual input format; the software does not claim to auto-detect BMD signal.
4. To collect comparison logs, open PowerShell in this directory and run:
   powershell -NoProfile -ExecutionPolicy Bypass -File .\\Start-BMD-Capture-Test.ps1
   Select device/format; it exits automatically 3 minutes later and generates a zip in logs. The script uses a private config, does not change original settings.
   Legacy negotiation comparison: append -Variant Legacy to the same command. For both, choose exactly the same device/mode/enhancement settings; do not run both at the same time.
5. If still black-screen, deliver the two zips together with the OBS-source properties (actual input format, card model, HDMI/SDI) to the developer. The black-pixel statistic in the log is not a "no signal" determination; a normal black scene does not trigger switch/frame-drop.

This round fixes the receiver and driver allocator/connection-format negotiation, keeping the driver DIB direction; at the same time it rejects silent subtype/interlaced-format contract changes. Local COM, GPU pixel, normal USB capture and GUI verification done; no local BMD card, so whether the physical card recovers output is still awaiting feedback. Runtime/models reused from the original package, this patch does not replace the driver.
Based on current main dbfceab (including the already-merged VFG optimization); this package is only a BMD local candidate, not a new version release. The in-software build string reuses the cached build's 2.0.6-vfg-next, with the EXE SHA in bmd-compat-manifest.json as this candidate identity.
'''
files={n:{'sha256':sha(p),'bytes':p.stat().st_size} for n,p in payload.items()}
meta={'schema':1,'version':'2.0.6','candidate':'bmd-followup-20261009','localOnly':True,'releaseReady':False,'baseMain':receipt['baseMain'],'displayVersion':'2.0.6-vfg-next','realBmdHardwareValidated':False,'runtimeChanges':False,'appliesTo':'Blackmagic/DeckLink native raw video; v210 keeps the existing system conversion path','correspondingSource':'Veyra-2.0.6-BMD-compat-source.zip','compiledSources':receipt['sources'],'files':files}
target=OUT/'Veyra-2.0.6-BMD-compat-test.zip';assert not target.exists()
with zipfile.ZipFile(target,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for name,p in sorted(payload.items()):z.write(p,name)
 z.writestr('USAGE.txt',readme.encode('utf-8-sig'));z.writestr('bmd-compat-manifest.json',json.dumps(meta,ensure_ascii=False,indent=2).encode('utf8'))
with zipfile.ZipFile(target) as z:
 assert z.testzip() is None
 for name,row in files.items():assert hashlib.sha256(z.read(name)).hexdigest()==row['sha256'],name
 assert not any(Path(n).suffix.lower() in ('.dll','.onnx','.pdb','.lib','.hipfb') for n in z.namelist())
auditDonor()
result={'path':str(target),'bytes':target.stat().st_size,'sha256':sha(target),'files':len(files)+2,'crcAndEveryPayloadShaVerified':True,'exeSha256':sha(BUILD/'veyra-bmd-followup.exe'),'donorFilesVerified':len(manifest['files'])}
(BASE/'reports'/TASK/'package.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(result,ensure_ascii=False))
