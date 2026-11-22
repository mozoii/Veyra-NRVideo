"""Require actual fresh evidence, document it and merge the reviewed release."""
from pathlib import Path
import ast,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
LOG=BASE/'logs'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def load(p):return json.loads((LOG/p).read_text(encoding='utf8'))
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')];subprocess.run(guard,check=True)
assert not (LOG/'main-merge.json').exists()
build=load('build-product.json');assert build['exit']==0
for n,h in build['sourceFiles'].items():assert sha(ROOT/n)==h,n
for name in ('amd-graph','amd-abi','effect-chain','units','gpu'):
 rows=load(name+'/summary.json');assert rows and all(r['passed'] and r['exit']==0 for r in rows),name
graphtext=(LOG/'amd-graph/veyra_amd_nr_graph_tests.log').read_text(encoding='utf8')
assert '20 cases, 0 failures' in graphtext and 'D3D12_DEBUG errors=0' in graphtext
assert '62 checks 0 failures' in (LOG/'amd-abi/veyra_lmxxf_nr_tests.log').read_text(encoding='utf8')
assert '913 checks 0 failures' in (LOG/'gpu/veyra_vfg_gpu_tests.log').read_text(encoding='utf8')
assert '331 checks, 0 failures' in (LOG/'units/veyra_vfg_settings_tests.log').read_text(encoding='utf8')
colors=(LOG/'gpu/veyra_hdr_color_tests.log').read_text(encoding='utf8');colorcount=len(re.findall(r'\bpass=1\b',colors));assert colorcount>=124 and not re.search(r'\bpass=0\b',colors)
perf=[load(n+'/result.json') for n in ('gta-medium4','gta-high2')]
for r,target in zip(perf,(120,60)):
 assert r['passed'] and not r['errors'] and r['fps']>=target*.97 and r['observations']>=17,r
 assert r['exeSha256']==sha(BASE/'build'/TASK/'veyra_qml_ui.exe')
 assert any('requested=2 actual=2' in line for line in r['normalSchedulingClass'])
exports=load('exports/summary.json');assert len(exports)==4 and all(r['exit']==(1 if r['expected']=='missing' else 0) for r in exports)
assert exports[0]['frames']==exports[1]['frames']==32
qr='5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd';assert sha(ROOT/'docs/images/2.0.6/community-group.png')==qr
for name in ('README.md','README_CN.md','docs/RELEASE_SUPPORT.md'):
 t=(ROOT/name).read_text(encoding='utf8');assert t.count('width="220"')==2 and 'https://discord.gg/c9aREyMj8' in t and 'https://ko-fi.com/likely7' in t
 assert '/v1.4.0/docs/images/1.4.0/donate-wechat.jpg' in t and '/v2.0.6/docs/images/2.0.6/community-group.png' in t
 assert ('community group 5' if name=='README.md' else 'community group 5') in t
for p in (ROOT/'scripts/acceptance').glob('release-2.0.6-*.py'):ast.parse(p.read_text(encoding='utf8'),filename=str(p))
testrows='\n'.join('| '+r['target']+' | '+str(round(r['seconds'],3))+' | exit0 / PASS |' for name in ('amd-graph','amd-abi','effect-chain','units','gpu') for r in load(name+'/summary.json'))
perftable='\n'.join('| '+name+' | '+str(r['fps'])+' | '+str(r['observations'])+' | '+str(round(r['wallSeconds'],3))+' |' for name,r in zip(('Medium4X','High2X'),perf))
report=f'''# Veyra 2.0.6 new build release acceptance

Product source base bf6e651 includes VFG, Blackmagic and NR->FSR fixes; this round's production change is only the CMake number/display version 2.0.6, the rest is release documentation/scripts/user-authorized QR codes. The fresh build uses the official 2.0.5 external-dependency cache; build-product.py's sourceFiles binds the final source per-hash ({len(build['sourceFiles'])} inputs), not a historical build substituting for this pass.

## What actually ran this round

`python -B scripts/acceptance/release-2.0.6-build.py build-product veyra_qml_ui veyra_amd_nr_graph_tests veyra_lmxxf_nr_tests veyra_effect_chain_tests veyra_vfg_gpu_tests veyra_vfg_settings_tests veyra_vfg_export_probe veyra_capture_color_tests veyra_capture_format_selection_tests veyra_hdr_color_tests`: exit0, {build['seconds']:.3f}s. EXE SHA256 {sha(BASE/'build'/TASK/'veyra_qml_ui.exe')}; version resource independently PE-checked as 2.0.6.0 at packaging. Compile kept existing C4244 conversion warnings, no build errors.

`release-2.0.6-amd-tests.py amd-graph after / amd-abi abi / effect-chain units`, `release-2.0.6-tests.py prepare / units / gpu` run in sequence, no simultaneous GPU work, private E-drive profile/TEMP. NR graph 20 cases 0 failures, 10 markers all pass / max code-value deviation 0 / D3D12 errors 0; ABI62 checks, effect chain 264 PASS; capture color 213 PASS, format selection 0 failures; GPU color {colorcount} items pass=1 including HDYC/flip 709; VFG913 checks/settings331 checks both 0 failures.

| Program | seconds | result |
|---|---:|---|
{testrows}

`release-2.0.6-preview.py case gta-medium4 --multiplier 4 --quality 1 --no-verbose` and `case gta-high2 --multiplier 2 --quality 2 --no-verbose`: new 2.0.6 EXE, user GTA4K30 video, single-layer original internal 1080p NR, 5s warmup + 20s observation; actual GPU request/effective class2 (normal), presentation queue priority0; no competing load. Both PASS / no error logs, normal state; readings are software-submission statistics:

| This round's retest | FPS median | 1-second sample count | process total seconds |
|---|---:|---:|---:|
{perftable}

This round only confirmed the new version runs normally; no new run of the pre-change binary. The +50.0%/+69.0% in the release note comes from the [VFG optimization report](VFG_OPTIMIZATION_REPORT_2026-10-07.md) adjacent matched experiment (80->120/35.5->60), not computed from this round's single value; the same core-source hash is traceable. Do not infer all cards, screen latency or VRAM gains.

`release-2.0.6-exports.py exports`: real NVENC HEVC export 720p30 medium4X, GTA4K30+original1080p NR medium4X, both 8 source frames -> 32 encoded frames, 120fps/CFR/strictly increasing PTS, duration ~8/30s; 4K size 3840x2160. Also cancellation and missing-runtime-library failure cleanup passes (missing runtime library exit1 is expected). Reads saved 8-frame GTA fixture only, does not modify the user's original video. Details of the four outputs/ffprobe/PTS: logs/exports/summary.json.

## Post-release gate

After a clean source commit and fast-forward-merge to main, package.py generates the two-vendor full packages plus application/dependency sources/verification for 5 assets in total; per-payload hashes/ZIP CRC/PE dependencies/vendor boundaries must pass. cold.py only unpacks from the final ZIP, runs basic playback with system PATH/private profile, and checks the package's Qt/FFmpeg modules. remote.py may only allow draft->publish after everything passes. The final-stage result is authoritative in E:/项目/Veyra/logs/{TASK}/source-audit.json, stage-AMD/NVIDIA.json, cold-verify-results.json, verify-draft.json, verify-public.json and the post-release main receipt; this document is fixed before packaging and must not fabricate cold-start/remote results that have not happened.

## Boundaries, review and protection

NR graph uses RTX5070 + real FSR3.1.5 + deterministic test NR provider, verifying result passing, and cannot substitute for AMD HIP/FSR4.1/Xbox physical card. Test providers stay in tests only, never shipped. Blackmagic has no local hardware; 2.0.6 did not rerun the prior USB-card connection acceptance and does not claim real HDMI restored. NVIDIA VRAM growth unresolved; Xbox independent hardware-decode fallback is out of scope.

No automatic product-test failures this round; the script-search stage read a nonexistent old script filename, changed to actual vfg-opt-export.py, which does not affect product verification. Release review fixed the README English QR alt from legacy group4 to group5. Existing compile warnings and test-provider fault-injection ERRORs both have original logs, and expected ABI errors are not misreported as real AMD inference failures. Any unexpected failure in other phases must be recorded separately, not overwrite the original receipt.

Baseline SHA256 ee8aab7b3484eabc90d55b59547dd5bd7934b14385ebff791c6f11b2eab3aba4; guard protected 33 other worktrees / 495 existing modifications / 3678 old files. New QR original-byte SHA256 {qr}, dual QR220, original sponsor/Discord/Ko-fi kept. No new SDK/model source added to Git, no modification of old artifacts/user profiles/drivers/other FG, no forwarding group messages/shutdown.

All products in E:/项目/Veyra/{build,tests,logs,tmp,releases,verify,archives}/{TASK} respective directories. Actual commands, results, logs and sources are evidenced by this plan/acceptance and the JSON files; do not claim an independent Reviewer or unexecuted hardware acceptance.
'''
(ROOT/'docs/RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md').write_text(report,encoding='utf8')
p=ROOT/'docs/WORKLOG.md';s=p.read_text(encoding='utf8');header='# 当前工作日志';assert s.startswith(header)
s=s.replace(header,header+f'\n\n## 2026-10-07 2.0.6 official release construction\n\nUser explicitly authorized build, main merge, GitHub official release and switching to 5-group QR codes. Isolated codex/release-2.0.6-20261007 base bf6e651, main/nrvideo base af5bfc3; VFG/Blackmagic/AMD NR->FSR fixes kept original product bytes, CMake only changed version to 2.0.6. Fresh build {build["seconds"]:.3f}s exit0, graph20/ABI62/chain264/capture213/color{colorcount}/VFG913+settings331 and four export checks passed; GTA normal-priority medium4X{perf[0]["fps"]}FPS/high2X{perf[1]["fps"]}FPS. Details, data, failure re-checks and untested hardware: [this round acceptance](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md), [release plan](RELEASE_2.0.6_PLAN_2026-10-07.md). New group QR original bytes/valid until Oct 14, original sponsor/Discord/Ko-fi kept. All products in E:/项目/Veyra/{TASK} respective directories. 33 old worktrees/495 modifications/3678 evidence files protected by independent guard. Later packaging/final ZIP cold-start/remote publish: appended with the actual receipt; NVIDIA VRAM growth unresolved.\n',1);p.write_text(s,encoding='utf8')
subprocess.run(guard,check=True);subprocess.run(['git','-C',str(ROOT),'diff','--check'],check=True)
assert git(MAIN,'rev-parse','HEAD')=='af5bfc3a66afce3047868229a3070a91c0fc9c22' and not git(MAIN,'status','--porcelain')
assert git(ROOT,'ls-remote','--heads','nrvideo','refs/heads/main').split()[0]==git(MAIN,'rev-parse','HEAD')
subprocess.run(['git','-C',str(ROOT),'add','--','AGENTS.md','CMakeLists.txt','README.md','README_CN.md','docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_SUPPORT.md','docs/images/2.0.6/community-group.png','docs/BUILD_2.0.6.md','docs/RELEASE_NOTES_2.0.6.md','docs/RELEASE_2.0.6_PLAN_2026-10-07.md','docs/RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md',*[str(p.relative_to(ROOT)).replace('\\','/') for p in sorted((ROOT/'scripts/acceptance').glob('release-2.0.6-*.py'))]],check=True)
subprocess.run(['git','-C',str(ROOT),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(ROOT),'commit','-m','Release Veyra 2.0.6 with VFG, NR/FSR and capture fixes'],check=True)
source=git(ROOT,'rev-parse','HEAD');assert not git(ROOT,'status','--porcelain')
before=git(MAIN,'rev-parse','HEAD')
subprocess.run(['git','-C',str(MAIN),'merge','--no-ff',source,'-m','Merge validated Veyra 2.0.6 release'],check=True)
after=git(MAIN,'rev-parse','HEAD');assert git(MAIN,'rev-parse','HEAD^{tree}')==git(ROOT,'rev-parse','HEAD^{tree}')
(LOG/'main-merge.json').write_text(json.dumps(dict(mainBefore=before,mainAfter=after,sourceCommit=source),indent=2),encoding='utf8')
subprocess.run(guard,check=True);print('REVIEWED RELEASE SOURCE AND MAIN',source,after,flush=True)
