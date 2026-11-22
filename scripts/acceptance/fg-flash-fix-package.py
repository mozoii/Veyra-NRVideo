"""Local overlay with exact corresponding source; no runtime/SDK/model upload."""
from pathlib import Path
import hashlib,io,json,os,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='fg-flash-fix-20261009'
OUT=BASE/'test-packages'/TASK;OUT.mkdir(parents=True,exist_ok=True)
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,encoding='ascii').strip()
exe=BASE/'build'/TASK/'veyra-hdr-flash-fix.exe'
receipt=json.loads((BASE/'logs'/TASK/'build/receipt.json').read_text(encoding='utf8'))
assert sha(exe)==receipt['executables'][str(exe)]['sha256']
productSources={rel.replace('\\','/'):digest for rel,digest in receipt['sources'].items() if rel.replace('\\','/').startswith(('src/','include/'))}
assert set(productSources)=={'include/veyra/engine/VideoPresenter.h','src/engine/VideoPresenter.cpp','src/engine/EngineController.cpp','src/engine/EngineControllerImage.cpp'}
for rel,digest in productSources.items():assert sha(ROOT/rel)==digest,rel
source=OUT/'Veyra-2.0.6-HDR-flash-fix-source.zip';assert not source.exists()
subprocess.run(['git','archive','--format=zip','--prefix=Veyra-hdr-flash-fix/','--output='+str(source),'HEAD'],cwd=ROOT,check=True)
with zipfile.ZipFile(source) as z:
 assert z.testzip() is None
 for n in z.namelist():assert Path(n).suffix.lower() not in ('.dll','.lib','.exe','.pdb','.onnx','.cubin','.hipfb'),n
 for rel,digest in productSources.items():
  assert z.read('Veyra-hdr-flash-fix/'+rel).replace(b'\r\n',b'\n')==(ROOT/rel).read_bytes().replace(b'\r\n',b'\n'),rel
stage=OUT/'overlay';stage.mkdir(exist_ok=False)
shutil.copy2(exe,stage/'veyra_qml_ui.exe')
shutil.copy2(ROOT/'scripts/diagnostics/Start-HDR-Flash-Fix.ps1',stage/'Start-HDR-Flash-Fix.ps1')
os.link(source,stage/source.name)
for name in ('LICENSE','LICENSE.md','LICENSE.txt','THIRD_PARTY_NOTICES.md'):
 if (ROOT/name).is_file():shutil.copy2(ROOT/name,stage/name)
for name,variant in [('01-Test-Compatible.cmd','Compatible'),('02-Test-Direct-Control.cmd','Direct')]:
 (stage/name).write_text('@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-HDR-Flash-Fix.ps1" -AppDirectory "%~dp0." -Variant '+variant+'\npause\n',encoding='ascii')
text='''Veyra 2.0.6 - HDR frame-gen flicker-fix compatibility candidate (2026-10-09)

This is a rollback-capable test candidate; verified locally; whether flicker disappears on the user RTX5090/capture card still awaits re-testing, cannot be called fully fixed.

Usage:
1. Close all Veyra. First copy the original directory's veyra_qml_ui.exe to veyra_qml_ui.original.exe.bak and keep it, then overwrite the package into the complete 2.0.6 NVIDIA portable directory. Do not rename the EXE, do not run it in a bare directory missing Qt/runtime libraries.
2. Beforehand, save the same input and picture-quality/frame-generation settings in the original build. Double-click 01-Test-Compatible.cmd and use the 4K HDR capture that used to flicker, source timestamps and frame-generation settings. It does not close itself by default; close manually when done.
3. Double-click 02-Test-Direct-Control.cmd and re-test with the same input/settings. It runs the same EXE, same path, only closing the new copy path. Both launchers copy the original settings to a private directory, so in-test changes do not write back; do not change driver configuration between the two runs.
4. Logs and receipts are automatically packaged in logs/hdr-flash-*.zip, containing only logs, no private config/credentials. Feedback: both ZIPs and whether flicker happened. Compatible no-flicker + Direct flicker supports the presentation-path hypothesis; both no-flicker still does not prove root-cause fix.

Changed: NVIDIA HDR10 + DLSS/VFG/app-side FSR frame-gen, first GPU-composite to a dedicated texture then copy to the swap chain. Default SDR, no frame-gen, AMD, XeSS keep direct presentation. No pixel sampling, extra CPU wait or automatic multiplier reduction. The 4K extra texture is fixed at about 31.9 MiB.
This machine GTA6 4K input/1080p original NR/DLSS2X test: both paths playback rate 1.000, source skip 0; 1280x720 composite GPU window P95 median 0.034 -> 0.039 ms. This data cannot be extrapolated to 5090 or panel scan.

Based on current main, includes the VFG optimization already merged earlier. Direct only represents the current main original presentation path and cannot fully replace the old 2.0.6 full-package comparison.
No runtime/model swap, NVIDIA App/RTSS or driver-setting changes. If changing only the EXE name stops flicker, another program-level configuration needs to be investigated, not credited to this candidate.

Rollback: close Veyra and copy the backup EXE back to veyra_qml_ui.exe; DLLs and original user settings are kept. Double-clicking the standard EXE uses the original settings per normal product behavior; the two test launchers use private copies.
Exact complete source: same package Veyra-2.0.6-HDR-flash-fix-source.zip; source and build identity in hdr-flash-fix-manifest.json. Full 2.0.6 dependency/license: reused from the original package.
'''
(stage/'USAGE.txt').write_text(text,encoding='utf-8-sig')
shutil.copy2(ROOT/'docs/FG_FLASH_FIX_REPORT_2026-10-09.md',stage/'INVESTIGATION-AND-TEST-REPORT.md')
manifest={'kind':'local-hdr-flash-compatibility-candidate','version':'2.0.6','sourceCommit':commit,'baseMain':'dbfceabee804cd81bac4a4781f0b7b4cf272902e','baseProduct':'71723e0249a17764696796fd204a6eb69d6da276','executableSha256':sha(exe),'sourceArchive':source.name,'sourceSha256':sha(source),'runtimeIncluded':False,'pixelProbeIncluded':False,'field5090Validated':False}
(stage/'hdr-flash-fix-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
files={p.name:sha(p) for p in stage.iterdir() if p.is_file()}
target=OUT/'Veyra-2.0.6-HDR-flash-fix-test.zip';assert not target.exists()
with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for name in sorted(files):z.write(stage/name,name,compress_type=zipfile.ZIP_STORED if name.endswith('.zip') else zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(target) as z:
 assert z.testzip() is None
 for name,digest in files.items():assert hashlib.sha256(z.read(name)).hexdigest()==digest,name
 assert z.namelist().count('veyra_qml_ui.exe')==1
 assert json.loads(z.read('hdr-flash-fix-manifest.json'))==manifest
assert sha(stage/'veyra_qml_ui.exe')==sha(BASE/'tests'/TASK/'app/veyra_qml_ui.exe')
result={'sourceCommit':commit,'package':{'path':str(target),'bytes':target.stat().st_size,'sha256':sha(target),'files':files},'source':{'path':str(source),'bytes':source.stat().st_size,'sha256':sha(source)},'passed':True}
(BASE/'reports'/TASK/'package-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in result.items() if k!='package'},ensure_ascii=False));print('Package:',target.stat().st_size,sha(target))
