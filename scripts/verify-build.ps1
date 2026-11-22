<#
.SYNOPSIS
  Post-build verification: unit tests + deployment consistency + zero runtime log errors.

.DESCRIPTION
  Covers the three classes of problems exposed by the 2.1.4 upgrade on 2026-11-10; run after every build:
    1. Unit tests (preset library encode/decode, effect chain, repair contract) - catches version-number/self-check regressions.
    2. Deployment consistency (the deployed exe must equal the just-built exe) - prevents only the source being changed without the program.
    3. Runtime start/exit, looking only at the lines this run added - catches errors that only appear when it runs
       (e.g. 2.1.4's session-encoding check failure, QML bindings to deleted properties).

  Any failure exits with code 1 and prints the failing item.

.PARAMETER BuildDir
  Build directory, must be given explicitly.

.PARAMETER DeployDir
  Deployed runtime directory, must be given explicitly. Verification only writes the task evidence directory.

.PARAMETER SkipBuild
  Skip the build, run only the tests and runtime checks.

.PARAMETER RunSeconds
  Runtime check launch duration, default 8 seconds (--exit-after).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/verify-build.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/verify-build.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$DeployDir,
    [string]$EvidenceDir = 'E:/项目/Veyra/tests/verify-build',
    [string]$VcVars = '',
    [switch]$SkipBuild,
    [ValidateRange(1,240)][int]$RunSeconds = 8
)

$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]

function Fail([string]$msg) {
    $failures.Add($msg) | Out-Null
    Write-Host "FAIL  $msg" -ForegroundColor Red
}
function Ok([string]$msg) {
    Write-Host "OK    $msg" -ForegroundColor Green
}
function Info([string]$msg) {
    Write-Host "      $msg" -ForegroundColor DarkGray
}

Write-Host '=== 1/4 Build ===' -ForegroundColor Cyan
if ($SkipBuild) {
    Info 'Skipped (-SkipBuild)'
} else {
    $vcvars = $VcVars
    if (-not $vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vs) { $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat' }
        }
    }
    if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found: $vcvars" }
    $targets = 'veyra_qml_ui veyra_preset_library_tests veyra_effect_chain_tests veyra_repair_contract_tests veyra_live_timing_tests'
    $cmd = "call `"$vcvars`" >nul && cmake --build `"$BuildDir`" --target $targets -j 2"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { Fail "Build failed (exit $LASTEXITCODE)" } else { Ok 'Build passed' }
}

Write-Host ''
Write-Host '=== 2/4 Unit tests ===' -ForegroundColor Cyan

# Some tests link FFmpeg dynamically (veyra_engine's AV libraries), and the DLL is not in the build directory:
# Find the FFmpeg root from CMakeCache and inject its bin into PATH, or the test exits with 0xC0000135.
$ffmpegBin = $null
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path $cache) {
    $m = Select-String -Path $cache -Pattern '^VEYRA_FFMPEG_ROOT:PATH=(.+)$' | Select-Object -First 1
    if ($m) {
        $root = $m.Matches[0].Groups[1].Value.Trim()
        $candidate = Join-Path $root 'bin'
        if (Test-Path $candidate) { $ffmpegBin = $candidate }
    }
}
if ($ffmpegBin) {
    $env:PATH = "$ffmpegBin;$env:PATH"
    Info "FFmpeg DLL: $ffmpegBin"
} else {
    Info 'FFmpeg bin not found; if a test reports 0xC0000135, check VEYRA_FFMPEG_ROOT in CMakeCache'
}
$tests = @('veyra_preset_library_tests', 'veyra_effect_chain_tests', 'veyra_repair_contract_tests', 'veyra_live_timing_tests')
foreach ($t in $tests) {
    $exe = Join-Path $BuildDir "$t.exe"
    if (-not (Test-Path $exe)) { Fail "$t.exe missing"; continue }
    # Run from the build directory: the test binaries load runtime DLLs relative to cwd.
    Push-Location $BuildDir
    try { $out = & $exe 2>&1 | Out-String; $rc = $LASTEXITCODE }
    finally { Pop-Location }
    $pass = ([regex]::Matches($out, 'PASS')).Count
    $fail = ([regex]::Matches($out, 'FAIL')).Count
    if ($rc -ne 0) {
        Fail "$t exited with $rc"
        $out -split "`n" | Select-String -Pattern 'FAIL' | Select-Object -First 5 | ForEach-Object { Info $_.Line.Trim() }
    } elseif ($fail -gt 0) {
        Fail "$t has $fail FAILs (PASS $pass)"
    } else {
        Ok "$t passed (PASS $pass)"
    }
}

Write-Host ''
Write-Host '=== 3/4 Deployment consistency ===' -ForegroundColor Cyan
$built = Join-Path $BuildDir 'veyra_qml_ui.exe'
$deployed = Join-Path $DeployDir 'veyra_qml_ui.exe'
$runtimeOk = $false
if (-not (Test-Path $built)) {
    Fail "Build artifact missing: $built"
} elseif (-not (Test-Path $deployed)) {
    Fail "Deploy directory missing or has no exe: $deployed"
} else {
    $h1 = (Get-FileHash $built -Algorithm SHA256).Hash.ToLower()
    $h2 = (Get-FileHash $deployed -Algorithm SHA256).Hash.ToLower()
    if ($h1 -ne $h2) {
        Fail "Deployed exe differs from the build (deployed $($h2.Substring(0,16)) / built $($h1.Substring(0,16))); deploy first, then verify"
    } else {
        Ok "Deployed exe matches ($($h1.Substring(0,16))…)"
        $runtimeOk = $true
    }
}

Write-Host ''
Write-Host '=== 4/4 Runtime log ===' -ForegroundColor Cyan
if (-not $runtimeOk) {
    Info 'Skipped (deployment inconsistent or missing)'
} else {
    $logsDir = Join-Path $EvidenceDir ('run-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
    $log = Join-Path $logsDir 'veyra-qml.log'
    $before = 0
    if (Test-Path $log) { $before = (Get-Item $log).Length }
    $dataDir = Join-Path $logsDir 'profile'
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    '{"introMusic":false,"introAnimation":false,"prewarmEnhancement":false}' | Set-Content -LiteralPath (Join-Path $dataDir 'qml-preferences.v1.json') -Encoding utf8
    $previousLog = $env:VEYRA_LOG_FILE
    $env:VEYRA_LOG_FILE = $log
    $proc = Start-Process -FilePath $deployed `
        -ArgumentList @('--data-dir', ('"' + $dataDir + '"'), '--exit-after', "$($RunSeconds * 1000)") `
        -WorkingDirectory $DeployDir -WindowStyle Hidden -PassThru -Wait
    $env:VEYRA_LOG_FILE = $previousLog
    if ($proc.ExitCode -ne 0) {
        Fail "Runtime start exited with $($proc.ExitCode)"
    } else {
        Ok "Runtime start/exit clean (--exit-after $RunSeconds)"
    }
    Start-Sleep -Milliseconds 500
    $text = ''
    if (Test-Path $log) {
        $fs = [System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        try {
            if ($fs.Length -gt $before) {
                $fs.Position = $before
                $sr = New-Object System.IO.StreamReader($fs)
                $text = $sr.ReadToEnd()
                $sr.Dispose()
            }
        } finally { $fs.Dispose() }
    }
    $errors = @($text -split "`n" | Where-Object { $_ -match '\[ERROR\]' })
    $warns  = @($text -split "`n" | Where-Object { $_ -match '\[WARN \]' })
    if ($errors.Count -gt 0) {
        Fail "$($errors.Count) ERROR lines this run"
        $errors | Select-Object -First 5 | ForEach-Object { Info $_.Trim() }
    } else {
        Ok '0 ERROR lines this run'
    }
    if ($warns.Count -gt 0) {
        Info "Plus $($warns.Count) WARN lines (not fatal; check by eye)"
        $warns | Select-Object -First 5 | ForEach-Object { Info $_.Trim() }
    }
}

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host '=== All passed ===' -ForegroundColor Green
    exit 0
} else {
    Write-Host "=== $($failures.Count) failures ===" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
