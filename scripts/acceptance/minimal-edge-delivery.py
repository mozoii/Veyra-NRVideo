"""Small local EXE patch and read-only isolation audit; no full runtime duplication."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra')
TASK='minimal-edge-hdr-review-20261004'
LOGS=BASE/'logs'/TASK
OUT=BASE/'test-packages'/TASK
REFERENCE=BASE/'test-packages/vfg-integration-20261003/Veyra-2.0.2-vfg-20261003-win64-portable'
DESKTOP=Path('C:/Users/123/Desktop/Veyra DLSS Video Player')
START=BASE/'archives/field-upgrade-20261003-start'
EXPECTED_BASE='be4da2ff173aa74de8e2e1d5ecc73d40a23584ba8898ffd534e636a8793b3d6b'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd,stderr=subprocess.PIPE)
def normalized(value):return value.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')

subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/minimal-edge-control.py')],check=True)
LOGS.mkdir(parents=True,exist_ok=True)
if sys.argv[1]=='audit':
    start=json.loads((START/'manifest.json').read_text(encoding='utf-8-sig'))
    untracked=[row for row in start['files'] if row['path'].replace('\\','/').startswith('desktop-untracked/')]
    isolation={
        'desktopStatusNormalizedEqual':normalized(git('status','--porcelain','--branch',cwd=DESKTOP))==normalized((START/'desktop-status.txt').read_bytes()),
        'workingPatchBytesEqual':git('diff','--binary',cwd=DESKTOP)==(START/'desktop-working.patch').read_bytes(),
        'indexPatchBytesEqual':git('diff','--cached','--binary',cwd=DESKTOP)==(START/'desktop-index.patch').read_bytes(),
        'untrackedSourceHashesEqual':all(digest(DESKTOP/row['path'].replace('\\','/').split('/',1)[1]).upper()==row['sha256'].upper() for row in untracked),
        'mainPreserved':git('rev-parse','main').decode().strip()=='66cd3e50590766e5a654528ab61e491dc4d30c83',
    }
    manifest=json.loads((REFERENCE/'package-manifest.json').read_text(encoding='utf-8-sig'))
    rows=manifest['files']
    isolation['basePortablePayloadHashesEqual']=all(digest(REFERENCE/row['path']).lower()==row['sha256'].lower() for row in rows)
    isolation['baseExeHashEqual']=digest(REFERENCE/'veyra_qml_ui.exe')==EXPECTED_BASE
    LOGS.joinpath('source-isolation.json').write_text(json.dumps(isolation,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(isolation,indent=2))
    assert all(isolation.values()),'Isolation audit failed'
    print('ISOLATION PASS',len(untracked),'untracked sources;',len(rows),'original payloads unchanged')
elif sys.argv[1]=='patch':
    assert not git('status','--porcelain').strip(),'Commit tested source before packaging'
    assert digest(REFERENCE/'veyra_qml_ui.exe')==EXPECTED_BASE
    stage=OUT/'Veyra-2.0.2-minimal-edge-20261004-exe-patch'
    stage.mkdir(parents=True,exist_ok=False)
    shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',stage/'veyra_qml_ui.exe')
    note='''# Minimal-mode right-edge pixel-gap repair patch

  Only for the already-extracted Veyra-2.0.2-vfg-20261003-win64-portable test package.
  Exit Veyra and its export tasks first, then overwrite the package's same-name veyra_qml_ui.exe with the patch's EXE.
  Only the program is replaced; the original Qt, AMD NR, VFG, shaders, models and licenses keep using the existing package.
  This patch cannot run standalone, not for the public 2.0.2 or other versions. For rollback, restore the same-name EXE from the original ZIP.

  Fixes native window/region missing one column and the integer-pixel rounding black border when minimal auto-fits the film.
  Real-ratio black bars, fullscreen, original pixels and user scaling are kept. HDR/Dolby Vision PR not implemented/merged.
  RTX5070/616.56, Qt scale factor100/125/150/175/200%, GPU/software interface, window and overlay targeted short test pass; not done: real display DPI back-and-forth or new HDR/FG picture-quality acceptance.

  Original package EXE SHA256: '''+EXPECTED_BASE+'\n'+'New EXE SHA256: '+digest(stage/'veyra_qml_ui.exe')+'\n'
    (stage/'README_PATCH.md').write_text(note,encoding='utf-8')
    record={'schema':'veyra.local.exe-patch.v1','localOnly':True,'releaseReady':False,
            'sourceCommit':git('rev-parse','HEAD').decode().strip(),
            'requiresBaseCandidate':'2.0.2-vfg-20261003','requiresBaseExeSha256':EXPECTED_BASE,
            'baseWorkerAbi':8,'workerAbi':8,'hdrPrProductChanges':False,
            'files':[{'path':p.name,'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(stage.iterdir())]}
    (stage/'patch-manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    archive=OUT/(stage.name+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(stage.iterdir()):z.write(p,p.name)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert hashlib.sha256(z.read('veyra_qml_ui.exe')).hexdigest()==digest(stage/'veyra_qml_ui.exe')
    record.update(archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=digest(archive),
                  appSha256=digest(stage/'veyra_qml_ui.exe'),zipCrcPass=True)
    (OUT/'DELIVERY.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(record,ensure_ascii=False,indent=2))
else:raise SystemExit('Use audit or patch')
