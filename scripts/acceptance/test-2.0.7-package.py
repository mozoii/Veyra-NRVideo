"""Local 2.0.7 test delivery. Reuse audited vendor runtimes without modifying them.
stage -> seal -> cold; no GitHub access, driver installation or user profiles.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys,time,zipfile
import pefile
parser=argparse.ArgumentParser()
parser.add_argument('mode',choices=('stage','seal','cold'))
parser.add_argument('--task',default='test-2.0.7-20261012')
parser.add_argument('--label',default='2.0.7-test')
parser.add_argument('--nvidia-donor',default='E:/项目/Veyra/test-packages/fixes-20261010/Veyra-2.0.6-fixes-20261010-NVIDIA-win64-portable')
parser.add_argument('--amd-donor',default='E:/项目/Veyra/releases/release-2.0.6-20261007/packages/Veyra-2.0.6-AMD-win64-portable')
parser.add_argument('--validation-record',default='docs/TEST_2.0.7_REPORT_2026-10-12.md')
parser.add_argument('--cold-run',help='Separate verification run directory; preserves earlier evidence')
args=parser.parse_args()
assert re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,79}',args.task)
assert re.fullmatch(r'2\.0\.7-[A-Za-z0-9.-]+',args.label)
assert re.fullmatch(r'docs/[A-Za-z0-9_.-]+\.md',args.validation_record)
assert args.cold_run is None or re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}',args.cold_run)
ROOT=Path(__file__).resolve().parents[2];B=Path('E:/项目/Veyra');TASK=args.task;LABEL=args.label
VALIDATION=args.validation_record
BUILD=B/'build'/TASK;OUT=B/'test-packages'/TASK;REPORT=B/'reports'/TASK
FF=B/'deps/ffmpeg-subtitle-formats-20261009/installed'
FFSOURCE=B/'test-packages/subtitle-formats-20261009/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip'
FFSHA='49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6'
DEPSOURCE=B/'releases/release-2.0.6-20261007/Veyra-2.0.6-dependency-source.zip'
DEPSHA='4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9'
SOURCE=OUT/('Veyra-'+LABEL+'-source.zip')
DONORS={'NVIDIA':Path(args.nvidia_donor),'AMD':Path(args.amd_donor)}
assert all(p.resolve().is_relative_to(B.resolve()) for p in DONORS.values())
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
pkg=module('qml_package',ROOT/'scripts/package-qml-release.py')
vendor=module('vendor_package',ROOT/'scripts/package-vendor-release.py')
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a],text=True,encoding='utf8').strip()
def write(p,data):
    p.parent.mkdir(parents=True,exist_ok=True);swap=p.with_name(p.name+'.swap');swap.write_bytes(data);os.replace(swap,p)
def document(p,d):write(p,(json.dumps(d,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
def atomic(src,dst):write(dst,src.read_bytes())
def stagepath(flavor):return OUT/f'Veyra-{LABEL}-{flavor}-win64-portable'
def verify_donor(flavor):
    path=DONORS[flavor];data=json.loads((path/'package-manifest.json').read_text('utf8'))
    for r in data['files']:assert (path/r['path']).stat().st_size==r['size'] and sha(path/r['path'])==r['sha256'],r['path']
    return data
def payload(app):
    rows=[]
    for f in sorted(app.rglob('*')):
        if not f.is_file() or f.name=='package-manifest.json':continue
        rel=f.relative_to(app).as_posix();pkg.validate_payload(rel)
        assert f.name not in ('SrMotion.dxil','SrStaticDetect.dxil'),rel
        rows.append(dict(path=rel,size=f.stat().st_size,sha256=sha(f)))
    assert pkg.REQUIRED<={r['path'] for r in rows}
    return rows
def audit(app,flavor):
    names=vendor.check_vendor(app,flavor)
    manifest=json.loads((app/'package-manifest.json').read_text('utf8'))
    assert manifest['files']==payload(app)
    assert set(names)=={r['path'] for r in manifest['files']}|{'package-manifest.json'}
    assert sha(app/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
    assert json.loads((app/'release-runtime-manifest.json').read_text('utf8'))['enforcedAtRuntime'] is False
    pe=pefile.PE(str(app/'veyra_qml_ui.exe'));fixed=pe.VS_FIXEDFILEINFO[0]
    version=lambda a,b:f'{a>>16}.{a&65535}.{b>>16}.{b&65535}'
    versions=dict(file=version(fixed.FileVersionMS,fixed.FileVersionLS),product=version(fixed.ProductVersionMS,fixed.ProductVersionLS));pe.close()
    assert versions==dict(file='2.0.7.0',product='2.0.7.0'),versions
    files={n:app/n for n in names};packaged={f.name.lower() for f in files.values() if f.suffix.lower()=='.dll'}
    system=Path(os.environ['WINDIR'])/'System32';imports=[];missing=[]
    driver={'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}
    for n,f in files.items():
        if f.suffix.lower() not in ('.exe','.dll'):continue
        pe=pefile.PE(str(f),fast_load=True);pe.parse_data_directories(directories=[1,13])
        direct=[r.dll.decode().lower() for r in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])];delay=[r.dll.decode().lower() for r in getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[])];pe.close()
        assert not any(d.startswith('npp') for d in direct+delay),(n,direct,delay)
        for d in direct+delay:
            if d in packaged or d.startswith(('api-ms-','ext-ms-')) or (system/d).is_file() or d in driver:continue
            missing.append(dict(file=n,dependency=d,delay=d in delay))
        imports.append(dict(file=n,direct=direct,delay=delay))
    assert not missing,missing
    receipt=dict(passed=True,flavor=flavor,files=len(manifest['files']),runtimeRows=len(json.loads((app/'release-runtime-manifest.json').read_text('utf8'))['files']),peVersions=versions,exeSha256=sha(app/'veyra_qml_ui.exe'),peFiles=len(imports),imports=imports,missing=missing)
    document(REPORT/('payload-'+flavor+'.json'),receipt);return receipt
def refresh(flavor):
    app=stagepath(flavor);atomic(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
    for f in (ROOT/'qml').rglob('*'):
        if f.is_file() and (f.suffix in ('.qml','.js') or f.name=='qmldir'):atomic(f,app/'qml'/f.relative_to(ROOT/'qml'))
    for f in (BUILD/'shaders').glob('*.dxil'):atomic(f,app/'shaders'/f.name)
    for name in ('README.md','README.en.md','LICENSE','THIRD_PARTY_NOTICES.md'):
        if (ROOT/name).is_file():atomic(ROOT/name,app/name)
    docs={r['path'] for r in json.loads((DONORS[flavor]/'package-manifest.json').read_text('utf8'))['files'] if r['path'].startswith('docs/')}
    docs|={'docs/'+n for n in ('TEST_2.0.7_PLAN_2026-10-12.md','TEST_2.0.7_REPORT_2026-10-12.md','BUILD_2.0.7_TEST.md','XESS_FLOW_REVIEW_2026-10-12.md','XESS_HANDOFF_REPORT_2026-10-12.md','FG_CAPTURE_RECOVERY_REPORT_2026-10-12.md','PRO_VIDEO_GEOMETRY_REPORT_2026-10-12.md','PR34_CAPTURE_SETTINGS_REPORT_2026-10-12.md','DLSS45_SR_REPORT_2026-10-11.md')}
    docs|={VALIDATION,'docs/CONTENT_CADENCE_EXACT_2026-10-12.md'}
    for n in docs:
        if (ROOT/n).is_file():atomic(ROOT/n,app/n)
    ff=pkg.audit_ffmpeg(FF)
    for r in ff['files']:atomic(FF/'bin'/r['name'],app/r['name'])
    atomic(FF/'share/ffmpeg/veyra-local-build.json',app/'licenses/FFMPEG-VEYRA-BUILD.json')
    for src,dst in [('copyright','ZLIB-COPYRIGHT.txt'),('vcpkg.spdx.json','ZLIB-SPDX.json')]:atomic(FF/'share/zlib'/src,app/'licenses'/dst)
    runtime=json.loads((DONORS[flavor]/'release-runtime-manifest.json').read_text('utf8'));runtime.update(package='2.0.7',candidate=LABEL,localOnly=True)
    for n in ('release-runtime-manifest.json','runtime/experimental/release-runtime-manifest.json'):document(app/n,runtime)
    write(app/'GPU-PACKAGE.txt',f'Veyra {LABEL} / {flavor}\nLocal test build. Extract the entire package to a new directory and run veyra_qml_ui.exe. Unsupported features remain disabled. No drivers or SDK development files are bundled.\n'.encode('utf8'))
    notes=f'''# Veyra {LABEL} · {flavor}

Extract the package to a new directory and run veyra_qml_ui.exe. Do not just replace the old package's EXE. This is a local test build, not yet published as a GitHub release.

Priority tests:
- Fixed "capture 60→30" restores inter-frame sampling and does not participate in content dedup; automatic detection now compares full-frame pixels, avoiding the old sparse sampling that missed small motion. Only when content dedup is on does the comparison work increase; a whole-frame change still within capture-noise tolerance may be treated as a duplicate. The earlier T911 NV12/P010 hardware record is in docs/CONTENT_CADENCE_EXACT_2026-10-12.md.
- Capture card first selects resolution, then the actually supported frame rate and format. MJPEG is marked red "(not recommended)"; after changing resolution/frame rate, the pixel format must be reselected. Invalid combinations raise an error rather than silently switching format. An existing valid previous setting is still restored.
- XeSS + motion-flow "edge shield" switch (on by default), can be turned off for comparison. Includes the NVIDIA two-way check and the FidelityFX motion-boundary fix; enabling it has a compute cost and does not guarantee quality gains in every scene.
- Frame-generation persistence with source timestamps, pro-mode playback-bar boundary, capture-settings save/continue-last/at-startup restore.

Retains subtitles, playlist/folder import, DLSS SR K/M/L and VFG mixed quality tiers from the main line. HDR frame-generation compatibility and native Blackmagic capture remain default-off switches; real-hardware validation is needed. UI stats stay at 33 ms / state refresh at 250 ms and the default GPU priority is kept.
Build and regression for this build: {VALIDATION}; the earlier full integration record is in docs/TEST_2.0.7_REPORT_2026-10-12.md. The AMD package completed content and launch checks, which does not mean AMD hardware enhancement validation. NVIDIA long-run VRAM growth remains unresolved.

Corresponding source in the parent directory: Veyra-{LABEL}-source.zip; also provides the unchanged dependency source Veyra-2.0.6-dependency-source.zip and the new subtitle FFmpeg source FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip. Retains the H.264 slices256 patch and zlib subtitle support. Build: docs/BUILD_2.0.7_TEST.md.
The runtime manifest records author-original and community identities; community HashMismatch/NotSigned is recorded as-is, without locking user-replaced compatibility DLLs. No runtime libraries, SDKs, models or private media are bundled into the application source.
'''
    write(app/'README-TEST.en.md',notes.encode('utf8'))
def manifest(flavor,source=False):
    app=stagepath(flavor);build=json.loads(Path(json.loads((REPORT/'build.json').read_text('utf8'))['receipt']).read_text('utf8'))
    data=dict(schema=3,version='2.0.7',candidate=LABEL,displayVersion=LABEL,gpuPackage=flavor,localOnly=True,releaseReady=False,
        sourceArchiveCommit=git('rev-parse','HEAD'),mainCommit=git('rev-parse','main'),productSourceCommit=build['head'],exeSha256=sha(BUILD/'veyra_qml_ui.exe'),
        validationRecord=VALIDATION,peVersions=dict(file='2.0.7.0',product='2.0.7.0'),
        knownIssues=['NVIDIA long-run VRAM growth unresolved','AMD/Intel enhancement and Blackmagic field validation require affected hardware','XeSS boundary refinement adds GPU work; scene-dependent quality'],
        correspondingSource=dict(application='../'+SOURCE.name,applicationSha256=sha(SOURCE) if source else None,
            dependencies='../'+DEPSOURCE.name,dependencySha256=DEPSHA,ffmpeg='../'+FFSOURCE.name,ffmpegSha256=FFSHA,
            instructions='docs/BUILD_2.0.7_TEST.md',displayVersionOverride='-DVEYRA_DISPLAY_VERSION='+LABEL),files=payload(app))
    document(app/'package-manifest.json',data);return data
def stage():
    assert not git('status','--porcelain'),'Commit before staging'
    for flavor in DONORS:
        app=stagepath(flavor);assert not app.exists(),app;donor=verify_donor(flavor);app.mkdir(parents=True)
        for r in donor['files']:
            pkg.validate_payload(r['path']);target=app/r['path'];target.parent.mkdir(parents=True,exist_ok=True);os.link(DONORS[flavor]/r['path'],target)
        refresh(flavor);manifest(flavor);audit(app,flavor);verify_donor(flavor);print('STAGE PASS',flavor,app,flush=True)
def source_archive():
    assert not git('status','--porcelain') and not SOURCE.exists()
    assert git('rev-parse','HEAD^{tree}')==git('rev-parse','main^{tree}'),'Seal integrated main only'
    records=[];skipped=[];prefix=SOURCE.stem+'/'
    with zipfile.ZipFile(SOURCE,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for n in filter(None,git('ls-files','-z').split('\0')):
            pkg.relative(n)
            if '__pycache__' in n or n.endswith('.pyc'):skipped.append(n);continue
            assert not re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|zip|7z|ptx|addon64)$|(^|/)(runtime_local|third_party_local|\.git)/',n,re.I),n
            f=ROOT/n;assert not f.is_symlink(),n;data=f.read_bytes();z.writestr(prefix+n,data);records.append(dict(path=n,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
        z.writestr(prefix+'source-snapshot-manifest.json',json.dumps(dict(commit=git('rev-parse','HEAD'),mainCommit=git('rev-parse','main'),displayVersion=LABEL,files=records,excludedTrackedCache=skipped),ensure_ascii=False,indent=2))
    with zipfile.ZipFile(SOURCE) as z:
        assert z.testzip() is None
        for r in records:assert hashlib.sha256(z.read(prefix+r['path'])).hexdigest()==r['sha256'],r['path']
    return dict(path=str(SOURCE),sha256=sha(SOURCE),files=len(records),excludedTrackedCache=skipped)
def seal():
    source=source_archive();rows=[]
    for p,h in [(FFSOURCE,FFSHA),(DEPSOURCE,DEPSHA)]:
        assert sha(p)==h
        with zipfile.ZipFile(p) as z:assert z.testzip() is None
        linked=OUT/p.name
        if not linked.exists():os.link(p,linked)
        assert sha(linked)==h
    for flavor in DONORS:
        verify_donor(flavor);refresh(flavor);data=manifest(flavor,True);result=audit(stagepath(flavor),flavor)
        app=stagepath(flavor);archive=Path(str(app)+'.zip');assert not archive.exists()
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for r in data['files']:z.write(app/r['path'],app.name+'/'+r['path'])
            z.write(app/'package-manifest.json',app.name+'/package-manifest.json')
        checked=pkg.verify_zip(archive);rows.append(dict(vendor=flavor,app=str(app),archive=str(archive),sha256=checked['sha256'],bytes=archive.stat().st_size,payloadFiles=result['files'],peFiles=result['peFiles'],runtimeRows=result['runtimeRows'],exeSha256=result['exeSha256']))
        verify_donor(flavor);print('ZIP PASS',flavor,archive,flush=True)
    for p in [SOURCE,OUT/FFSOURCE.name,OUT/DEPSOURCE.name,*[Path(r['archive']) for r in rows]]:write(Path(str(p)+'.sha256'),(sha(p)+'  '+p.name+'\n').encode('ascii'))
    document(OUT/'DELIVERY.json',dict(localOnly=True,version=LABEL,mainCommit=git('rev-parse','main'),source=source,packages=rows,ffmpegSource=FFSOURCE.name,dependencySource=DEPSOURCE.name))
def cold():
    # These are playback/startup checks with effects off, not performance tests.
    # Preserve an already running user session; reject only this harness's own
    # unfinished verification processes and record shared-GPU conditions.
    running=json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',"[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false); ConvertTo-Json -Compress -InputObject @((Get-Process | Where-Object { $_.ProcessName -like 'veyra*' } | Select-Object Id,ProcessName,Path))"],text=True,encoding='utf8'))
    own=B/'verify'/TASK
    assert not any(r.get('Path') and Path(r['Path']).resolve().is_relative_to(own.resolve()) for r in running),running
    rows=json.loads((OUT/'DELIVERY.json').read_text('utf8'))['packages'];results=[]
    for row in rows:
        flavor=row['vendor'];dest=B/'verify'/TASK
        if args.cold_run:dest=dest/args.cold_run
        dest=dest/flavor;assert not dest.exists() and dest.resolve().is_relative_to((B/'verify'/TASK).resolve())
        archive=Path(row['archive']);assert sha(archive)==row['sha256']
        pkg.verify_zip(archive)
        with zipfile.ZipFile(archive) as z:
            for r in z.infolist():pkg.relative(r.filename)
            z.extractall(dest)
        app=dest/Path(row['app']).name;audit(app,flavor)
        for mode in ('d3d12','software'):
            tag='cold-'+(args.cold_run+'-' if args.cold_run else '')+flavor+'-'+mode;profile=B/'tests'/TASK/tag;tmp=B/'tmp'/TASK/tag;logs=B/'logs'/TASK/tag
            for p in (profile,tmp,logs):p.mkdir(parents=True,exist_ok=False)
            document(profile/'qml-preferences.v1.json',dict(language='zh-CN',autoResume=False,rememberPosition=False,prewarmEnhancement=False,gpuPriority='normal',sentinel='retained'))
            env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};win=Path(env['WINDIR'])
            env.update(PATH=str(win/'System32')+';'+str(win),TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(logs/'engine.log'))
            if mode=='software':env['QT_QUICK_BACKEND']='software'
            else:env['VEYRA_UI_RHI']='d3d12'
            command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x900','--exit-after','14000','E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4']
            if mode=='software':command.insert(1,'--obs-game-capture')
            start=time.monotonic()
            with (logs/'console.log').open('wb') as f:run=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=50)
            text='\n'.join(p.read_text('utf8',errors='replace') for p in [logs/'engine.log',logs/'console.log'] if p.exists())
            errors=[l for l in text.splitlines() if re.search(r'ReferenceError:|TypeError:|Cannot assign|Binding loop|D3D12 ERROR|D3D12 CORRUPTION|\[ERROR\s*\]|\[FATAL\s*\]|no root object',l)]
            presented=[int(v) for v in re.findall(r'\brealPresented=(\d+)',text)]
            assert run.returncode==0 and not errors and presented and max(presented)>=100,(tag,run.returncode,errors,presented)
            assert ('interface renderer=d3d12' in text) if mode=='d3d12' else ('interface=software' in text),(tag,'requested renderer was not active')
            assert json.loads((profile/'qml-preferences.v1.json').read_text('utf8'))['sentinel']=='retained'
            audit(app,flavor);results.append(dict(vendor=flavor,mode=mode,passed=True,exit=run.returncode,seconds=round(time.monotonic()-start,1),realPresented=max(presented),logs=str(logs),archiveSha256=row['sha256'],exeSha256=sha(app/'veyra_qml_ui.exe')))
            document(OUT/'cold-start.json',dict(passed=len(results)==4,results=results,existingUserProcesses=running,performanceMeasurement=False));print('COLD ZIP PASS',flavor,mode,flush=True)
    document(REPORT/'package.json',dict(passed=True,delivery=str(OUT/'DELIVERY.json'),cold=str(OUT/'cold-start.json')))
OUT.mkdir(parents=True,exist_ok=True);REPORT.mkdir(parents=True,exist_ok=True)
{'stage':stage,'seal':seal,'cold':cold}[args.mode]()
