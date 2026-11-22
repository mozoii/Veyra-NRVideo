"""Record completed publication in main without changing the release tag/tree."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='publish-2.0.5-20261007'
LOG=BASE/'logs'/TASK;OUT=BASE/'releases'/TASK;MAIN=BASE/'worktrees/main-merge-20261002'
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.5-control.py')],check=True)
remote=json.loads((LOG/'verify-public.json').read_text(encoding='utf8'));assert remote['passed'] and remote['latest']
merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));head=merge['mainAfter']
assert git(MAIN,'rev-parse','HEAD')==head and not git(MAIN,'status','--porcelain')
assert not (LOG/'documentation-close.json').exists()
cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'))
assets=json.loads((LOG/'assets.json').read_text(encoding='utf8'))
table='\n'.join('| '+r['name']+' | '+str(r['bytes'])+' | '+r['sha256']+' |' for r in assets)
report=f'''# Veyra 2.0.5 official release receipt

Published: {remote['url']}, GitHub Release ID {remote['releaseId']}, published_at {remote['publishedAt']}. Official, non-prerelease, GitHub latest check passes. Release tag v2.0.5 points at main merge {head}, release source commit {merge['sourceCommit']}; tag/source tree identical. Closing documents are committed separately, not touching the tag or assets.

Product keeps the tested 2.0.5: EXE SHA256 acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936, PE File/ProductVersion2.0.5.0. EXE/QML/shaders and the NVIDIA215/AMD694 original runtime-component bytes are kept; this round only has release documentation, scripts and metadata. 1724 baseline source inputs, 20 current acceptance receipts, 28 old worktrees/495 modified files/53 historical evidence files protected by an independent guard.

## Actual commands and results

Python3.11 -B ran publish-2.0.5-control.py: pass; package.py: two complete packages, application/dependency sources, 5-asset manifest done, per-payload SHA and ZIP CRC pass; audit.py: per-file manifest, vendor isolation, PE import closure pass; cold.py: two final ZIPs unpacked independently, basic playback and embedded-subtitle launch with only system PATH/private profile pass. Cold start: {'; '.join(r['vendor']+' '+str(r['seconds'])+'s/exit0/'+str(len(r['ownQtAndCodecModulePaths']))+' in-package Qt/FFmpeg modules' for r in cold)}. All RTX5070 local basic runs, cannot substitute for AMD HIP inference/encoding acceptance.

remote.py push/draft/verify-draft/publish/verify-public: normal atomic push of main+tag, draft uploads 5 assets, remote all sizes and sha256 digests match the local copy; the body is verified verbatim, tag and main match, official latest verification passes. Actually re-downloading SHA256SUMS matches the local copy; both the 5 public assets/Release page and the dual QR and Ko-fi button are HTTP-reachable. Dual QR width220, Discord/Ko-fi kept; old v2.0.4 ID/body/time/assets unchanged. No forwarding of group messages or shutdown.

All receipts: E:/项目/Veyra/logs/{TASK}/, including source-audit.json, stage-AMD/NVIDIA.json, cold-verify-results.json, main-merge.json, push.json, verify-draft.json, verify-public.json and remote-public-release.json. Products E:/项目/Veyra/releases/{TASK}/; source bundle at archives/{TASK}/source-final.bundle and already verified.

## Failures and re-checks

The strict guard at start found 13 inherited files converted to CRLF in the new checkout; all content was first confirmed to differ only by line endings, then the protected main original bytes were restored in the new owned worktree and the clean-filter blob was confirmed equal to HEAD. The old baseline/other worktrees were unchanged, after which the guard passed. The original record checkout-line-endings.json is kept. The only historical tracked pyc is kept in Git, explicitly excluded from the source ZIP; no existing source files were modified/deleted.

## Official assets

| File | bytes | SHA256 |
|---|---:|---|
{table}

Dependency source is byte-identical to the 2.0.4 original package (4eccde63...), renamed this round and shipped with the Release; patched FFmpeg, static subtitles and streaming source are fully kept. Source has no proprietary SDK/runtime/model; user packages have no dev LIB/PDB/test media/config/logs.

## Data and boundaries

fix2 same-EXE stats-animation reverse comparison, NR interval down 5.80%, UI submission down 89.06%, limited only to RTX5070/616.56/320Hz, GTA4K30, original single-NR internal 1080, normal priority. vs 2.0.3 matched +0.009%/-0.638%. The new 2.0.5 single-round 6.295ms is not stitched into a new speedup claim; UI submission does not equal latency/VRAM. NVIDIA VRAM growth still unlocated/unfixed, investigated next version; AMD real high-resolution HIP offline export/encoding, physical HDR/tearing/VRR and long-term multi-device stability are not all accepted. XeSS frame-gen/node offline export still unsupported. The announcement also keeps these boundaries.
'''
(MAIN/'docs/PUBLISH_2.0.5_REPORT_2026-10-07.md').write_text(report,encoding='utf8')
p=MAIN/'docs/WORKLOG.md';s=p.read_text(encoding='utf8');header='# 当前工作日志';assert s.startswith(header)
s=s.replace(header,header+'\n\n## 2026-10-07 2.0.5 official release complete\n\n'+
    f'Official release: {remote["url"]} (Release ID {remote["releaseId"]}, {remote["publishedAt"]}), 5 assets remote size/SHA256 digest match, latest official version, corresponding source/dependency source, body/dual QR and download availability all checked. Both final ZIPs cold-start and 26 in-package module paths each pass, product bytes kept. Actual commands, results, failure re-checks, hashes and log paths: [PUBLISH_2.0.5_REPORT](PUBLISH_2.0.5_REPORT_2026-10-07.md). main merge {head}, source {merge["sourceCommit"]}; only the official receipt is committed after that, not touching tags/assets/product. NV VRAM unfixed; announcement <=1000 chars, only to the user, not forwarded or shutdown.\n',1);p.write_text(s,encoding='utf8')
p=MAIN/'docs/CURRENT_STATUS.md';s=p.read_text(encoding='utf8');header='# Current status';assert s.startswith(header)
s=s.replace(header,header+'\n\n## 2026-10-07 current public official version 2.0.5\n\n'+
    f'[v2.0.5]({remote["url"]}) is released and verified as latest; NVIDIA/AMD full portable packages, application/dependency sources and SHA256SUMS total 5 assets, remote digest matches. Product keeps the tested 2.0.5, official cold-start passes, see [release receipt](PUBLISH_2.0.5_REPORT_2026-10-07.md). NVIDIA VRAM growth still unlocated/unfixed, AMD physical-card offline encoding still awaiting retest; the records below represent only the state at the time.\n',1);p.write_text(s,encoding='utf8')
changed=set(git(MAIN,'diff','--name-only').splitlines())|set(git(MAIN,'ls-files','--others','--exclude-standard').splitlines())
assert changed=={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/PUBLISH_2.0.5_REPORT_2026-10-07.md'},changed
subprocess.run(['git','-C',str(MAIN),'diff','--check'],check=True)
subprocess.run(['git','-C',str(MAIN),'add','--',*sorted(changed)],check=True)
subprocess.run(['git','-C',str(MAIN),'commit','-m','docs: record verified Veyra 2.0.5 publication'],check=True)
after=git(MAIN,'rev-parse','HEAD');save(LOG/'documentation-close.json',dict(releaseMainCommit=head,mainAfter=after,files=sorted(changed)))
subprocess.run(['git','-C',str(MAIN),'push','nrvideo','main:main'],check=True)
announcement='''Veyra 2.0.5 official release is out!

This round mainly fixes stability and performance regressions: fixes OBS compatibility-mode rolling smear, AMD NR black-screen compositing and multi-layer NR export; AMD/Intel frame-gen defaults to FSR priority, unavailable old DLSS config is auto-migrated. FSR video export is no longer forcibly switched to DLSS. Automatic display sync covers window and fullscreen, and adapts HDR display tuning, logs and build fixes from PR19/20.

This machine RTX5070, GTA VI 4K input, original single-layer 1080p NR, normal GPU priority, same-EXE stats-animation comparison: NR processing interval down about 5.8%, UI submission reduced about 89%. vs 2.0.3 same-condition comparison is close to level; these are this-machine-limited numbers, not all-card gains, nor latency or VRAM reduction.

Download the NVIDIA/AMD full package for your card and extract to a new directory, keeping the old version for fallback. NR strength 5, auto/manual control and list/node unified presets are kept.

Known: NVIDIA VRAM growth still unfixed, to be deeply investigated next version; AMD physical-card offline encoding awaiting retest, XeSS frame-gen offline export still unsupported. Feedback: attach card, driver, input source, effect settings and log.

Download: https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.5
Discord: https://discord.gg/c9aREyMj8
Sponsor: https://ko-fi.com/likely7
'''
assert len(announcement)<1000
(OUT/'GROUP_ANNOUNCEMENT.txt').write_text(announcement,encoding='utf8')
save(LOG/'finish.json',dict(passed=True,mainCommit=after,releaseTagCommit=head,announcementCharacters=len(announcement),announcement=str(OUT/'GROUP_ANNOUNCEMENT.txt')))
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.5-control.py')],check=True)
print('PUBLICATION DOCUMENTATION COMPLETE',after,len(announcement),'announcement characters',flush=True)
