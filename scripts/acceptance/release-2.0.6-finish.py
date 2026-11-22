"""Close the verified publication with a documentation-only main commit."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
LOG=BASE/'logs'/TASK;OUT=BASE/'releases'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')];subprocess.run(guard,check=True)
remote=json.loads((LOG/'verify-public.json').read_text(encoding='utf8'));assert remote['passed'] and remote['latest']
merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));head=merge['mainAfter']
assert git(MAIN,'rev-parse','HEAD')==head and not git(MAIN,'status','--porcelain')
assert not (LOG/'documentation-close.json').exists()
cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'));assets=json.loads((LOG/'assets.json').read_text(encoding='utf8'))
table='\n'.join('| '+r['name']+' | '+str(r['bytes'])+' | '+r['sha256']+' |' for r in assets)
report=f'''# Veyra 2.0.6 official release receipt

Published: {remote['url']}; Release ID {remote['releaseId']}, published_at {remote['publishedAt']}, non-prerelease, latest check passes. Tag v2.0.6 points at main merge {head}, release source {merge['sourceCommit']}, trees identical. Closing documents committed separately, not touching the tag, product or assets.

## Build and regression

Per [release acceptance](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md): full new 2.0.6 product/test build, NR->FSR graph 20, ABI 62, effect chain 264, capture color 213/format negotiation, GPU color 124, VFG GPU 913/settings 331, four NVENC exports (720p4X, 4K NR4X, cancel, missing runtime library) and GTA medium4X/high2X normal-priority preview. Build source input bound per-hash to the final source; PE numeric version 2.0.6.0. Real AMD HIP/FSR4.1/Xbox and Blackmagic HDMI not accepted on the local physical card, NVIDIA sustained VRAM growth unfixed.

package.py generates the complete package and corresponding source from the clean release source; audit.py passes for all manifest files of the two vendors, runtime-component identity, driver exclusion and PE dependency closure; ZIP CRC and all payload SHA pass. Final ZIP cold start: {'; '.join(r['vendor']+' '+str(r['seconds'])+'s/exit0/'+str(len(r['ownQtAndCodecModulePaths']))+' in-package Qt/FFmpeg modules' for r in cold)}. All RTX5070 local basic playback, cannot substitute for AMD hardware inference/encoding.

remote.py push/draft/verify-draft/publish/verify-public: normal atomic push of main+tag, draft uploads 5 assets, server size/digest each match the local copy; body verbatim identical, tag/main identical, official latest check passes. Re-downloaded SHA256SUMS byte-identical, 5 assets/page/dual QR/Ko-fi button HTTP200. New group 5 QR remote SHA256 equals the user's original image 5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd, dual QR each width220, original sponsor URL, Discord and Ko-fi kept. Old v2.0.5 ID/body/publish time/assets fully kept.

## Assets

| File | bytes | SHA256 |
|---|---:|---|
{table}

Dependency source byte-identical to 2.0.5 (4eccde63...), only renamed 2.0.6; patched FFmpeg, static dependency source and relink material kept. Application source ZIP comes from the clean release commit, only excluding one historical tracked pyc; the original file in Git kept. Source has no SDK/runtime/model, user packages have no test provider/log/media/personal profile/PDB/LIB.

## Protection and paths

Execution receipt: E:/项目/Veyra/logs/{TASK}/; final assets: E:/项目/Veyra/releases/{TASK}/; independent unpack: E:/项目/Veyra/verify/{TASK}/; build/test/tmp each in the same task subdirectory. archives/{TASK}/start.json fixes the start bytes, source-before/final.bundle can restore the source. Guard re-checks 33 other worktrees, 495 existing modifications and 3678 old artifacts/evidence items; main only allows this merge and the three-file closing commit, does not modify the old baseline. Actual failures and corrections in the release acceptance. Group announcement written to GROUP_ANNOUNCEMENT.txt, only to the user, not forwarded or shutdown.

## Data boundaries

VFG +50.0%/+69.0% comes from the earlier adjacent matched RTX5070/GTA4K30/single-layer1080p NR/normal-priority comparison; the new 2.0.6 single-round retest is recorded separately, not stitched into a new causal percentage. The metric is software presentation-submission FPS, not physical refresh/screen latency/VRAM reduction. AMD graph regression uses real FSR3.1.5 and deterministic NR test provider, cannot pass off HIP inference.
'''
(MAIN/'docs/RELEASE_2.0.6_REPORT_2026-10-07.md').write_text(report,encoding='utf8')
for name,header,entry in [
 ('WORKLOG.md','# 当前工作日志',f'## 2026-10-07 2.0.6 official release complete\n\n[GitHub Release]({remote["url"]}) is out (ID {remote["releaseId"]}, {remote["publishedAt"]}), 5 assets server SHA256/size match, latest official version, corresponding source, dual QR and public download all verified. New group 5 QR original bytes, sponsor/Discord/Ko-fi kept. Full commands, results, asset hashes, boundaries and paths: [release receipt](RELEASE_2.0.6_REPORT_2026-10-07.md). Tag {head}, source {merge["sourceCommit"]}; only the three-file closing doc is appended after that. NV VRAM growth unresolved.'),
 ('CURRENT_STATUS.md','# Current status',f'## 2026-10-07 current public official version 2.0.6\n\n[v2.0.6]({remote["url"]}) is released and verified latest. VFG scheduling, AMD NR->FSR output, Blackmagic compatibility merged; NVIDIA/AMD full packages, application/dependency sources and SHA256SUMS total 5 assets remote digest match, final ZIP cold-start passes. See [release receipt](RELEASE_2.0.6_REPORT_2026-10-07.md). 5-group QR replaced, image noted valid until Oct 14. NV VRAM growth unresolved, AMD/Blackmagic physical-card retest boundaries in acceptance; the records below represent only their time state.')]:
 p=MAIN/'docs'/name;s=p.read_text(encoding='utf8');assert s.startswith(header);p.write_text(s.replace(header,header+'\n\n'+entry+'\n',1),encoding='utf8')
changed=set(git(MAIN,'diff','--name-only').splitlines())|set(git(MAIN,'ls-files','--others','--exclude-standard').splitlines())
assert changed=={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_2.0.6_REPORT_2026-10-07.md'},changed
subprocess.run(['git','-C',str(MAIN),'diff','--check'],check=True)
subprocess.run(['git','-C',str(MAIN),'add','--',*sorted(changed)],check=True)
subprocess.run(['git','-C',str(MAIN),'commit','-m','docs: record verified Veyra 2.0.6 publication'],check=True)
after=git(MAIN,'rev-parse','HEAD');save(LOG/'documentation-close.json',dict(releaseMainCommit=head,mainAfter=after,files=sorted(changed)))
subprocess.run(guard,check=True)
subprocess.run(['git','-C',str(MAIN),'push','nrvideo','main:main'],check=True)
announcement='''Veyra 2.0.6 official release is out!

This round mainly optimizes VFG frame generation, fixes the issue where the AMD NR result did not enter the subsequent FSR processing, and adds Blackmagic/DeckLink capture compatibility.

VFG no longer makes SDK submission work occupy the presentation thread for a long time. This machine RTX5070, GTA VI 4K30 input, original single-layer1080p NR, normal GPU priority, same-condition comparison: medium4X from 80 to 120 FPS, up 50%; high-tier2X from 35.5 to 60 FPS, up about 69%. This is a limited test of software submission frames, not all-card gains, and high multipliers may still be limited by GPU budget.

AMD: fixes the issue where NR visibly ran but FSR/scaling re-read the original image, so toggling NR did not change the picture. Blackmagic: adds WDM connection, HDYC color format and driver-attribute entry; physical AMD cards and real Blackmagic inputs welcome continuing feedback.

Download the NVIDIA or AMD full package for your card and extract to a new directory, keeping the old version for fallback. Known: NVIDIA VRAM growth still unresolved; this round does not claim it fixed.

The 4th group is full, switched to 5 groups, image noted valid until Oct 14. Original WeChat sponsor, Discord and Ko-fi kept. Feedback: attach card, driver, input source, effect settings and log.

Download: https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.6
Discord: https://discord.gg/c9aREyMj8
Sponsor: https://ko-fi.com/likely7
'''
assert len(announcement)<1000;(OUT/'GROUP_ANNOUNCEMENT.txt').write_text(announcement,encoding='utf8')
save(LOG/'finish.json',dict(passed=True,mainCommit=after,releaseTagCommit=head,announcementCharacters=len(announcement),announcement=str(OUT/'GROUP_ANNOUNCEMENT.txt')))
subprocess.run(guard,check=True);print('PUBLICATION DOCUMENTATION COMPLETE',after,len(announcement),'characters',flush=True)
