"""Promote the user-approved ZIP bytes unchanged; no build/repack operations."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,zipfile
R=Path(__file__).resolve().parents[2];B=Path('E:/项目/Veyra');TASK='release-2.1.0-20261019'
P=B/'test-packages/bilibili-2.1.0-20261019';OUT=B/'releases'/TASK;REPORT=B/'reports'/TASK;ARCH=B/'archives'/TASK
for d in (OUT,REPORT,ARCH):d.mkdir(parents=True,exist_ok=True)
def git(*args):return subprocess.check_output(['git',*args],cwd=R,text=True,encoding='utf8').strip()
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def gh(*args):return json.loads(subprocess.check_output(['gh',*args],cwd=R,text=True,encoding='utf8'))
parser=argparse.ArgumentParser();parser.add_argument('mode',choices=('prepare','verify','remote'));a=parser.parse_args()
delivery=json.loads((P/'DELIVERY.json').read_text('utf8'))
items=[(P/Path(x['archive']).name,OUT/f"Veyra-2.1.0-{x['vendor']}-win64-portable.zip",x['sha256']) for x in delivery['packages']]
items.extend([(P/'Veyra-2.1.0-test-source.zip',OUT/'Veyra-2.1.0-source.zip',delivery['source']['sha256']),
              (P/'lmxxf-044-corresponding-source.zip',OUT/'lmxxf-044-corresponding-source.zip',delivery['amdNr044Source']['sha256'])])
QR=R/'docs/images/2.1.0/community-group.png'
if a.mode=='prepare':
    (ARCH/'refs-before.txt').write_text(git('show-ref'),'utf8');(ARCH/'worktrees-before.txt').write_text(git('worktree','list','--porcelain'),'utf8')
    (ARCH/'delivery-before.json').write_bytes((P/'DELIVERY.json').read_bytes())
    source=Path('C:/Users/123/AppData/Local/Temp/codex-clipboard-7b6a44f7-272b-4182-aa87-fafa74d98a8a.png')
    QR.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,QR);assert sha(source)==sha(QR)
    for src,dst,h in items:
        assert sha(src)==h,(src,'input digest')
        if not dst.exists():os.link(src,dst)
        assert sha(dst)==h,(dst,'promoted digest')
    logs=git('log','--reverse','--format=%H%x09%s','v2.0.9..d91ec9f').splitlines()
    paths=git('diff','--name-status','v2.0.9','d91ec9f').splitlines()
    nonmerge=git('rev-list','--no-merges','--count','v2.0.9..d91ec9f')
    index=['# 2.0.9 → 2.1.0 full change index','',f'Scope: v2.0.9 (08832f6) to verified main d91ec9f; {len(logs)} commits (non-merge {nonmerge}), {len(paths)} changed paths. Covers commits from both sides, cloud PRs, merges and acceptance docs; the commit titles below are not a per-feature claim. Product summary is in RELEASE_NOTES_2.1.0.md. The Lecram experiment is out of scope.','','## All commits','']
    index.extend(f'- `{x.split(chr(9))[0][:12]}` {x.split(chr(9),1)[1]}' for x in logs)
    index.extend(['','## All paths (A added / M modified / D deleted)','','```text',*paths,'```',''])
    (R/'docs/RELEASE_2.1.0_CHANGE_INDEX.md').write_text('\n'.join(index),'utf8')
    support=(R/'docs/RELEASE_SUPPORT.md').read_text('utf8').replace('v2.0.7/docs/images/2.0.7/community-group.png','v2.1.0/docs/images/2.1.0/community-group.png').replace('2026-10-19','2026-10-26')
    (R/'docs/RELEASE_SUPPORT.md').write_text(support,'utf8')
    notes=R/'docs/RELEASE_NOTES_2.1.0.md';body=notes.read_text('utf8')
    if '## Support & feedback' not in body:notes.write_text(body+'\n'+support,'utf8')
    source_body='''# Veyra 2.1.0 · Corresponding sources

[Return to the two software downloads](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0) · [Build and full dependency list](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/BUILD_2.1.0.md)

This page only stores sources, not another player package, and is not latest. Application source reuses the user-verified ZIP byte-for-byte, only with the -test suffix removed from the external filename.

- Veyra-2.1.0-source.zip: exact source 66b321051bef9343fa2bcc6556a53a551ccc809c, 2187 files, SHA256 `d3ed0860cebef310eeceb5fafd790b553f78cecdf2d393e9f4c4d6ca87b32559`.
- lmxxf-044-corresponding-source.zip: complete build inputs, pinned upstream source, patch, recipe and license for the AMD Small/Original two local modules, SHA256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`.

The following unchanged prior materials are still needed (fixed assets, not re-uploaded):

- [FFmpeg slices256/dav1d/zlib source](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip), SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`.
- [Independent deinterlace LGPL module source](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip), SHA256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`.
- [Dependency and relink material](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip), SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`.

Veyra is GPL-3.0; proprietary SDKs/runtimes/models do not enter source Git, and NVIDIA-derived weights do not become MIT because of the surrounding MIT sources. Original package licenses/runtime manifests are retained. The release tag only adds release docs, README, group image and release-verification scripts; it does not change the verified application inputs; GitHub-generated repository source is not the same as all dependency material.

English: These are the exact tested application sources and the corresponding sources for both AMD0.44 modules. Rebuild instructions, retained dependency/relinking sources and licenses are linked above. The main release has just the two GPU application packages. This source archive is not latest.
'''
    (R/'docs/RELEASE_SOURCE_2.1.0.md').write_text(source_body+'\n'+support,'utf8')
    print('prepared unchanged archives and release index; QR SHA256',sha(QR))
if a.mode in ('prepare','verify'):
    rows=[]
    for src,dst,h in items:
        assert sha(src)==sha(dst)==h
        with zipfile.ZipFile(dst) as z:
            assert z.testzip() is None
            names=z.namelist();exe=[n for n in names if n.endswith('/veyra_qml_ui.exe')]
            if exe:assert len(exe)==1 and hashlib.sha256(z.read(exe[0])).hexdigest()==delivery['packages'][0]['exeSha256']
        rows.append(dict(name=dst.name,bytes=dst.stat().st_size,sha256=h,files=len(names)))
    notes=(R/'docs/RELEASE_NOTES_2.1.0.md').read_text('utf8')
    for word in ('0.44','468','PR57','PR63','PR64','#48','0.93','1.42','2.1.0-test','10-26','c9aREyMj8','ko-fi.com/likely7','qcnv4i7tnd7l'):assert word in notes,word
    assert notes.count('width="220"')==2
    assert 'renodx' not in git('diff','--name-only','d91ec9f').lower()
    bad=[n for n in git('diff','--name-only','d91ec9f').splitlines() if n.startswith(('src/','include/','qml/','shaders/','apps/')) or n=='CMakeLists.txt']
    assert not bad,bad
    (REPORT/'local.json').write_text(json.dumps(dict(passed=True,archives=rows,qrSha256=sha(QR),applicationSource=delivery['mainCommit'],rebuild=False,repack=False),ensure_ascii=False,indent=2),'utf8')
    print('local verification passed',json.dumps(rows))
if a.mode=='remote':
    results=[]
    for tag,expected in [('v2.1.0',items[:2]),('v2.1.0-sources',items[2:])]:
        r=gh('release','view',tag,'--repo','Likely7/Veyra-NRVideo','--json','tagName,isDraft,isPrerelease,url,assets,body,targetCommitish')
        assets={x['name']:x for x in r['assets']};assert set(assets)=={dst.name for _,dst,_ in expected}
        for _,dst,h in expected:
            x=assets[dst.name];assert x['state']=='uploaded' and x['size']==dst.stat().st_size and x['digest']=='sha256:'+h
        for s in ('v2.1.0/docs/images/2.1.0/community-group.png','donate-wechat.jpg','c9aREyMj8','ko-fi.com/likely7','qcnv4i7tnd7l'):assert s in r['body']
        assert r['body'].count('width="220"')==2
        results.append(r)
    (REPORT/'remote.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf8')
    print('remote digests, sizes, exact asset sets and support blocks verified')
