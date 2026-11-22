[CmdletBinding()]
param(
    [string]$OutputDir,
    [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedSha256,
    [string]$Dumpbin
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if (-not $OutputDir) { $OutputDir = Join-Path $repo 'dist-windows-clean' }
$build = Join-Path $repo 'build-chatgpt\release-static'
$exe = Join-Path $build 'mrbones.exe'
$manifest = Join-Path $repo 'manifest\versions.txt'
$guide = Join-Path $repo 'docs\clean-windows-verification.md'

foreach ($file in @($exe, $manifest, $guide)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" }
}
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ExpectedSha256 -and $hash -ne $ExpectedSha256) {
    throw "Source executable SHA-256 mismatch: $hash"
}

if (-not $Dumpbin) {
    $cache = Join-Path $build 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $linker = Get-Content -LiteralPath $cache | Where-Object { $_ -like 'CMAKE_LINKER:FILEPATH=*' } | Select-Object -First 1
        if ($linker) { $Dumpbin = Join-Path (Split-Path -Parent ($linker -replace '^CMAKE_LINKER:FILEPATH=', '')) 'dumpbin.exe' }
    }
    if (-not $Dumpbin -or -not (Test-Path -LiteralPath $Dumpbin -PathType Leaf)) {
        $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
        if ($command) { $Dumpbin = $command.Source }
    }
}
if (-not $Dumpbin -or -not (Test-Path -LiteralPath $Dumpbin -PathType Leaf)) {
    throw 'dumpbin.exe is required on the packaging machine; pass -Dumpbin <path>.'
}
$report = & $Dumpbin /headers /dependents $exe 2>&1
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $report" }
$reportText = ($report -join "`n").ToLowerInvariant()
if ($reportText -notmatch '8664 machine \(x64\)') { throw 'mrbones.exe is not Windows x64.' }
$actual = @([regex]::Matches($reportText, '(?m)^\s*([a-z0-9_.+-]+\.dll)\s*$') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$allowed = @('bcrypt.dll', 'gdi32.dll', 'kernel32.dll', 'user32.dll', 'winmm.dll')
if ((Compare-Object $actual $allowed).Count -ne 0) {
    throw "Unexpected direct imports. Actual: $($actual -join ', '); allowed: $($allowed -join ', ')"
}

$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count -ne 0) {
    throw "Output directory is not empty: $output"
}
New-Item -ItemType Directory -Path (Join-Path $output 'manifest') -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $output 'mrbones.exe')
Copy-Item -LiteralPath $manifest -Destination (Join-Path $output 'manifest\versions.txt')
Copy-Item -LiteralPath $guide -Destination (Join-Path $output 'clean-windows-verification.md')
if ((Get-FileHash -LiteralPath (Join-Path $output 'mrbones.exe') -Algorithm SHA256).Hash -ne $hash) {
    throw 'Copied executable failed SHA-256 verification.'
}
Set-Content -LiteralPath (Join-Path $output 'mrbones.exe.sha256') -Value $hash -Encoding ASCII

$runner = @'
param(
    [ValidateSet('verify', 'check', 'lose', 'finale', 'campaign')][string]$Mode = 'check',
    [string]$Disc1Cue,
    [string]$Disc2Cue
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$exe = Join-Path $root 'mrbones.exe'
$manifest = Join-Path $root 'manifest\versions.txt'
$arch = $env:PROCESSOR_ARCHITECTURE
if ($env:PROCESSOR_ARCHITEW6432) { $arch = $env:PROCESSOR_ARCHITEW6432 }
if ($arch -ne 'AMD64') { throw 'Windows x64 is required.' }
$expected = (Get-Content -LiteralPath (Join-Path $root 'mrbones.exe.sha256') -Raw).Trim()
$actual = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw "mrbones.exe SHA-256 mismatch: $actual" }
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "Missing manifest: $manifest" }
if ($Mode -eq 'verify') {
    Write-Host "Windows x64, manifest and executable SHA-256 verified: $actual"
    exit 0
}
if (-not $Disc1Cue) { $Disc1Cue = Join-Path $root 'disco1\Mr. Bones (USA) (Disc 1).cue' }
if (-not $Disc2Cue) { $Disc2Cue = Join-Path $root 'disco2\Mr. Bones (USA) (Disc 2).cue' }
foreach ($cue in @($Disc1Cue, $Disc2Cue)) {
    if (-not (Test-Path -LiteralPath $cue -PathType Leaf)) {
        throw "Missing CUE: $cue. Supply -Disc1Cue and -Disc2Cue from your extracted discs."
    }
}
$Disc1Cue = (Resolve-Path -LiteralPath $Disc1Cue).ProviderPath
$Disc2Cue = (Resolve-Path -LiteralPath $Disc2Cue).ProviderPath
$saveDir = Join-Path $env:LOCALAPPDATA 'MrBonesPort\clean-test-saves'
New-Item -ItemType Directory -Path $saveDir -Force | Out-Null
$env:MB_CONFIG = Join-Path $env:LOCALAPPDATA 'MrBonesPort\clean-test.ini'
$gameArgs = @($Disc1Cue, $manifest, '--disc2', $Disc2Cue, '--save-dir', $saveDir)
if ($Mode -in @('check', 'campaign') -or $env:MB_MUTE -eq '1') {
    $env:MB_MUTE = '1'
    $gameArgs += '--mute'
}
switch ($Mode) {
    'check'    { $gameArgs += '--check' }
    'lose'     { $gameArgs += @('--level', '/28LOSE') }
    'finale'   { $gameArgs += @('--level', '/27FINALE') }
    'campaign' { $gameArgs += '--full-campaign' }
}
Write-Host "Running $Mode. Saves: $saveDir"
& $exe @gameArgs
exit $LASTEXITCODE
'@
Set-Content -LiteralPath (Join-Path $output 'run_tests.ps1') -Value $runner -Encoding UTF8
$batch = '@echo off' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*' + "`r`n" + 'exit /b %ERRORLEVEL%'
Set-Content -LiteralPath (Join-Path $output 'run_tests.bat') -Value $batch -Encoding ASCII
Write-Host "Package ready: $output"
Write-Host "mrbones.exe SHA-256: $hash"
Write-Host "Direct imports: $($actual -join ', ')"
