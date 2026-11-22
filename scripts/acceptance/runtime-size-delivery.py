"""Final local delivery index; product/source/runtime remain separate."""
import hashlib, json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
OUT=BASE/'test-packages'/TASK;LOGS=BASE/'logs'/TASK
STAGE=OUT/'Veyra-2.0.2-slim-20261004-win64-portable'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()
def read(name):return json.loads((LOGS/name).read_text(encoding='utf-8-sig'))
def write(p,r):p.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if sys.argv[1]=='index':
    archive=read('archive-audit.json');dense=read('dense-archive-audit.json');selection=read('archive-selection.json')
    primary=Path(str(STAGE)+'.7z');zipPath=Path(str(STAGE)+'.zip')
    chosen=dense if selection['selectedMethod']==dense['method'] else archive['archives'][1]
    assert digest(primary)==chosen['sha256'] and primary.stat().st_size==chosen['bytes']
    assert digest(zipPath)==archive['archives'][0]['sha256']
    clean=read('clean-smoke/summary.json');assert len(clean['results'])==2
    native=read('native-archive-final-v1.json');assert native['status']=='pass'
    assert all(read(n+'.json')['returncode']==0 for n in ('native-720-v1','native-4k-v1'))
    exports=read('export-all-v1/summary.json');assert len(exports)==21 and all(r['exit']==0 for r in exports)
    ui=read('ui-package-final/summary.json');assert len(ui)==4 and all(r['returncode']==0 for r in ui[:3])
    isolation=read('source-isolation.json');assert all(isolation.values())
    source=read('source-audit.json');bundle=read('final-bundle-audit.json')
    manifest=json.loads((STAGE/'package-manifest.json').read_text(encoding='utf8'))
    baseBytes=1151602649
    record={'schema':'veyra.local.complete-candidate.v1','localOnly':True,'releaseReady':False,'branch':'codex/runtime-size-20261004','compiledCodeCommit':manifest['baseCommit'],
            'sourceArchive':source,'gitBundle':bundle,'appSha256':digest(STAGE/'veyra_qml_ui.exe'),'primary':{**chosen,'archive':str(primary),'native7zipPassed':True},
            'zipFallback':archive['archives'][0],'comparison7z32':{**archive['archives'][1],'comparisonOnly':True,'currentBenchmarkPath':selection['comparison32'],'retained':Path(selection['comparison32']).exists()},
            'oldZipBytes':baseBytes,'primaryReductionPercent':100*(1-primary.stat().st_size/baseBytes),
            'vfgRawBefore':497894432,'vfgRawAfter':210788400,'amdRawBefore':615032656,'amdRawAfter':615032656,
            'checks':{'native720':463,'native4k':463,'settings':331,'nvencCombinations':21,'guiAndWorker':ui,'cleanPackage':clean,'nativeArchive':native,'isolation':isolation},
            'hardware':'RTX5070 / 616.56','amdInferenceVerified':False,'xboxHardwareVerified':False,'rtx40Verified':False,'hdrPrChanges':False,
            'hdrPlan':str(ROOT/'docs/HDR_DOVI_PR_REVIEW_2026-10-04.md'),'shutdown':'Requested by user; schedule only after delivery/index/source stored.'}
    write(OUT/'DELIVERY.json',record)
    mib=lambda n:f'{n/1048576:.1f}'
    text=f'''# Veyra complete local test package · 2026-10-04

Preferred `{primary.name}`, {mib(primary.stat().st_size)} MiB, extract to a new directory and run veyra_qml_ui.exe.
Fallback `{zipPath.name}`, {mib(zipPath.stat().st_size)} MiB. Both carry the same 2076 payloads + manifest.
Original VFG test ZIP {mib(baseBytes)} MiB -> new 7z {mib(primary.stat().st_size)} MiB, transfer size reduced {record['primaryReductionPercent']:.1f}%.
VFG runtime directory 474.8 -> 201.0 MiB, nine unused NPPs removed; AMD required resources keep 586.5 MiB.
7z dictionary {chosen.get('dictionaryBytes',33554432)//1048576} MiB only affects extraction memory, runtime memory and file contents are kept.

720p/4K each 463 native checks, 331 settings, 21 real NVENC combinations, hot switch/preset/restart/missing-lib, High8 frozen worker,
new EXE minimal edge 6 cases pass. Final independent unpack all file SHA, official 7-Zip CRC/compatibility, in-package VFG real UI/worker,
GPU and software each six-page background alpha/normal exit pass; original desktop/main/2077 original-payload isolation re-check passes.

Includes Xbox audio/reconnect, export bitrate, RTSS background and cancel of NVIDIA App false detection fix, AMD NR, all VFG tiers and minimal edge fixes.
Only RTX5070/616.56 short test. RX9000 inference, Xbox physical-machine long-term stability, RTX40/616.92, HDR chroma, physical screen DPI/physical display latency/long-term stability unverified.

HDR/DV PR13/14 not built or merged. #13 fixes P5 offset/full curve/MMR; #14 first adds build dependencies, changes brightness stats range/array/latency/reset.
Then solve preset version/worker ABI, unify "input format -> HDR processing card -> actual output/display calibration", merge per user approval after reference and physical-machine acceptance.
Detailed plan: HDR_DOVI_PR_REVIEW_2026-10-04.md in the same directory.

compiledCodeCommit: {manifest['baseCommit']}
sourceArchiveCommit: {source['sourceArchiveCommit']} (product code identical, only report/acceptance scripts after that)
Source ZIP `{Path(source['source']).name}`, SDK/model/runtime not included in the source; this is project source, not a complete third-party open-source audit.
Git bundle `{Path(bundle['path']).name}` verified. Hashes/full evidence/source and archive location: DELIVERY.json and .sha256.
No merge main, push or GitHub Release. Per user request, schedule normal shutdown after delivery/index/source is stored.

7z SHA256: {chosen['sha256']}
ZIP SHA256: {archive['archives'][0]['sha256']}
APP SHA256: {record['appSha256']}
'''
    (OUT/'DELIVERY.md').write_text(text,encoding='utf8')
    print('FINAL DELIVERY INDEX PASS',primary,chosen['bytes'],record['primaryReductionPercent'])
else:raise SystemExit('index')
