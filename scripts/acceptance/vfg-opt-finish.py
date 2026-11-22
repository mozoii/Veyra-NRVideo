"""Validate completed receipts and render the final local VFG report."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007';LOGS=BASE/'logs'/TASK
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-report.py')],cwd=ROOT,check=True)
exe=sha(BASE/'build'/TASK/'veyra_qml_ui.exe');matrix=[]
for q in ('low','medium','high'):
 for m in range(2,9):
  case=read(LOGS/f'{q}{m}-nr/result.json');assert case['passed'] and case['exeSha256']==exe and case['exitCode']==0,case
  assert case['config']['nr'] and case['config']['seconds']==20 and case['observations']>=17,case
  text=(LOGS/f'{q}{m}-nr/player.log').read_text(encoding='utf8',errors='replace')
  assert not re.search(r'\[ERROR\s*\]|\[FATAL\s*\]',text),q+str(m)
  matrix.append({'quality':q,'multiplier':m,'fps':case['fps'],'status':case['status']})
rows=['| Multiplier | source30 target fps | Low before->after | Medium before->after | High before->after |','| --- | ---: | ---: | ---: | ---: |']
for m in range(2,9):
 values=[]
 for q in ('low','medium','high'):
  old=read(BASE/'logs/vfg-diagnosis-20261007'/f'{q}{m}-nr/result.json');new=read(LOGS/f'{q}{m}-nr/result.json')
  suffix=' (dropped)' if any('降档' in s for s in new['status']) else ' (unmet)' if new['fps']<30*m*.99 else ''
  values.append(f'{old["fps"]:g}→{new["fps"]:g}{suffix}')
 rows.append(f'| {m}X | {30*m} | '+' | '.join(values)+' |')
rows+=['','"Before" is the previous official 2.0.5 diagnostic reference, "after" is this round final candidate 21 groups. Non-adjacent matrices are not used for precise causal attribution percentages; only the adjacent comparison below is used for gain calculation.']
controls=[];matched=['| Comparison | original fps | candidate fps | gain | original CPU rolling P95 median ms | candidate CPU rolling P95 median ms |','| --- | ---: | ---: | ---: | ---: | ---: |']
for name in ('medium4','high2'):
 old=read(LOGS/f'matched-old-{name}/analysis.json');new=read(LOGS/f'matched-new-{name}/analysis.json')
 assert old['result']['passed'] and new['result']['passed'] and new['result']['exeSha256']==exe
 assert old['result']['config']==new['result']['config']
 oldfps,newfps=old['result']['fps'],new['result']['fps'];gain=(newfps/oldfps-1)*100
 a=old['timing']['medianReportedWindowP95Ms']['graphSubmitP95Ms'];b=new['timing']['medianReportedWindowP95Ms']['graphSubmitP95Ms']
 controls.append({'case':name,'oldFps':oldfps,'newFps':newfps,'fpsGainPercent':gain,'oldCpuWindowP95Ms':a,'newCpuWindowP95Ms':b,'cpuWindowP95ReductionPercent':(1-b/a)*100,'oldCadence':old['submissionCadenceMs'],'newCadence':new['submissionCadenceMs'],'oldExpired':old['timing']['cumulativeDeltas']['expiredGenerated'],'newExpired':new['timing']['cumulativeDeltas']['expiredGenerated']})
 matched.append(f'| {name} | {oldfps:g} | {newfps:g} | {gain:.1f}% | {a:.3f} | {b:.3f} |')
sync=read(LOGS/'matched-sync-medium4/result.json');assert sync['passed'] and sync['syncSubmit'] and sync['exeSha256']==exe
matched+=['',f'The same candidate with only forced sync submission, control group Medium4, is {sync["fps"]:g}fps; the corresponding normal async group is {controls[0]["newFps"]:g}fps, supporting the causal explanation "the display thread was occupied by the SDK call".']
native=read(LOGS/'candidate-native-720-v3.json');assert native['exit']==0
native4k=read(LOGS/'async-native-4k-v2.json');assert native4k['exit']==0
hashes=read(LOGS/'native-hash-comparison.json');assert hashes['allGpuPixelHashesIdentical'] and hashes['intermediateFrames']==168 and hashes['finalCandidateHashMatches'] and hashes['finalCandidateExeSha256']==exe
exports=read(LOGS/'export-v3-verified/summary.json');assert len(exports)==24 and all(x['exit']==(1 if x['expected']=='missing' else 0) for x in exports)
life=[dict(x,evidence='lifecycle-v3') for x in read(LOGS/'lifecycle-v3/summary.json') if x['phase'] in ('run','restore')]
life += [dict(x,evidence='lifecycle-edge-v3') for x in read(LOGS/'lifecycle-edge-v3/summary.json')]
assert {x['phase'] for x in life}=={'run','restore','missing','failure'} and len(life)==4 and all(x['passed'] for x in life)
assert 'unavailable saved backend=NVIDIA-VFG selected=AMD-FSR' in (LOGS/'lifecycle-edge-v3/missing-engine.log').read_text(encoding='utf8')
gui=read(LOGS/'lifecycle-edge-v3/export-probe.json');guiVideo=next(x for x in gui['streams'] if x['codec_type']=='video');assert guiVideo['nb_read_frames']=='64' and guiVideo['r_frame_rate']=='240/1'
settings=read(LOGS/'settings-v3.json');assert settings['exit']==0 and '331 checks, 0 failures' in (LOGS/'settings-v3.log').read_text(encoding='utf-8-sig')
validation='\n'.join(['- Final candidate build candidate-build-v3 exit0, EXE SHA256 `'+exe+'`, core source ddf71b1.',
 '- 21 groups normal playback, all 5 adjacent/sync control sampling and exit pass; whether the target is met is judged by the table above.',
 '- 8/10 bit, three qualities, 2X-8X, cut/same-CUDA-address refresh/reverse motion/parity and 2X->4X->8X restore: 720p sync, 720p async, 4K async each 913 checks 0 failures, final EXE re-checks 720p pass; the 168 sync/async generated-frame GPU pixel hashes match.',
 '- GUI hot-switch quality/multiplier, VFG<->DLSS6X, pause/seek/scale, fullscreen enter/return, list/node preset and restart all pass; missing VFG runtime goes through the original launch migration to FSR2X and continues playback, not modifying that shared strategy. After an async Run rejection, the actual FG closes, original frames continue, no waiting hang. GUI-independent export worker keeps the sync SDK determination, 8 source frames out 64 frames/240fps.',
 '- VFG settings/preset/session and multiplier/PTS unit tests 331 checks 0 failures.',
 '- All 21 groups 720p three-quality x 2X-8X actual NVENC export and 4K GTA short film native NR+VFG4 export pass; per group 8 source frames, output 8x multiplier, HEVC/CFR and strictly increasing PTS verified, no source frames dropped. Edge hold still counts per the original export contract, not calling all outputs neural-generated frames. Cancel and missing-runtime export failure cleanup passes.',
 '- Failure record kept: the first two async-rejection assertions mistook the QML-saved fgEnabled request for the actual backend; the first missing-runtime assertion wrongly assumed the old strategy always closes FG, actually it migrated to FSR; the first eight-frame MP4 export assertion did not consider the microsecond time_base rounding (16 frames, nominal 60fps, average 59.999925fps). Tests corrected against the actual interface and time scale, then passed, without changing the product to accommodate tests. settings-tests initially forgot the required absolute output directory and exited 2, after adding the parameter all 331 checks pass.'])
path=ROOT/'docs/VFG_OPTIMIZATION_REPORT_2026-10-07.md';text=path.read_text(encoding='utf8')
text=text.replace('状态：验收进行中，以下已完成证据不代表尚未执行的检查通过。','Status: this machine VFG special short test passes, reaching some main-tier goals; higher multipliers still have clear limits.')
assert all(marker in text for marker in ('<!-- MATRIX_TABLE -->','<!-- MATCHED_TABLE -->','<!-- VALIDATION -->'))
text=text.replace('<!-- MATRIX_TABLE -->','\n'.join(rows)).replace('<!-- MATCHED_TABLE -->','\n'.join(matched)).replace('<!-- VALIDATION -->',validation)
path.write_text(text,encoding='utf8')
receipt={'buildExeSha256':exe,'buildSource':read(LOGS/'candidate-build-v3.json')['sourceHead'],'matrix':matrix,'matched':controls,'syncControlFps':sync['fps'],'nativeHashComparison':hashes,'exportCases':len(exports),'lifecycle':life,'settings':settings,'report':str(path)}
verify=BASE/'verify'/TASK;verify.mkdir(parents=True,exist_ok=True);(verify/'measurements.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'exeSha256':exe,'matched':controls,'matrixCases':len(matrix),'exports':len(exports)},ensure_ascii=False),flush=True)
