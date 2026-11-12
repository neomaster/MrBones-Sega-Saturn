[CmdletBinding()]
param(
    [string]$OutputDir,
    [string]$Disc1Cue,
    [string]$Disc2Cue,
    [string]$Dumpbin
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if (-not $OutputDir) { $OutputDir = Join-Path $repo 'dist-windows-clean' }
if (-not $Disc1Cue) { $Disc1Cue = Join-Path $repo 'disco1\Mr. Bones (USA) (Disc 1).cue' }
if (-not $Disc2Cue) { $Disc2Cue = Join-Path $repo 'disco2\Mr. Bones (USA) (Disc 2).cue' }
$exe = Join-Path $repo 'build-static\mrbones.exe'
$manifest = Join-Path $repo 'manifest\versions.txt'
$guide = Join-Path $repo 'docs\clean-windows-verification.md'

function Get-DiscFiles([string]$Cue) {
    $Cue = (Resolve-Path -LiteralPath $Cue).ProviderPath
    $references = @(
        Get-Content -LiteralPath $Cue | ForEach-Object {
            if ($_ -match '^\s*FILE\s+') {
                if ($_ -notmatch '^\s*FILE\s+"([^"]+)"\s+BINARY\s*$') {
                    throw "Unsupported CUE FILE entry in ${Cue}: $_"
                }
                $Matches[1]
            }
        } | Select-Object -Unique
    )
    if ($references.Count -eq 0) { throw "CUE has no BIN tracks: $Cue" }
    $files = @()
    foreach ($name in $references) {
        if ([IO.Path]::IsPathRooted($name) -or [IO.Path]::GetFileName($name) -ne $name -or
            [IO.Path]::GetExtension($name) -ine '.bin') {
            throw "Unsafe or unsupported track name in ${Cue}: $name"
        }
        $path = Join-Path (Split-Path -Parent $Cue) $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing BIN: $path" }
        $files += $path
    }
    return ,@($Cue) + $files
}

foreach ($file in @($exe, $manifest, $guide)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" }
}
$disc1Files = @(Get-DiscFiles $Disc1Cue)
$disc2Files = @(Get-DiscFiles $Disc2Cue)

if (-not $Dumpbin) {
    $cache = Join-Path $repo 'build-static\CMakeCache.txt'
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

$output = [IO.Path]::GetFullPath($OutputDir)
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count -ne 0) {
    throw "Output directory is not empty: $output"
}
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
foreach ($directory in @($output, (Join-Path $output 'manifest'),
                       (Join-Path $output 'disco1'), (Join-Path $output 'disco2'))) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Copy-Item -LiteralPath $exe -Destination (Join-Path $output 'mrbones.exe')
Copy-Item -LiteralPath $manifest -Destination (Join-Path $output 'manifest\versions.txt')
Copy-Item -LiteralPath $guide -Destination (Join-Path $output 'clean-windows-verification.md')
foreach ($file in $disc1Files) { Copy-Item -LiteralPath $file -Destination (Join-Path $output 'disco1') }
foreach ($file in $disc2Files) { Copy-Item -LiteralPath $file -Destination (Join-Path $output 'disco2') }
if ((Get-FileHash -LiteralPath (Join-Path $output 'mrbones.exe') -Algorithm SHA256).Hash -ne $hash) {
    throw 'Copied executable failed SHA-256 verification.'
}
Set-Content -LiteralPath (Join-Path $output 'mrbones.exe.sha256') -Value $hash -Encoding ASCII

$runner = @'
param([ValidateSet('check', 'lose', 'finale', 'campaign')][string]$Mode = 'check')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'mrbones.exe'
$manifest = Join-Path $root 'manifest\versions.txt'
$disc1 = Join-Path $root 'disco1\__DISC1_CUE__'
$disc2 = Join-Path $root 'disco2\__DISC2_CUE__'
$expected = (Get-Content -LiteralPath (Join-Path $root 'mrbones.exe.sha256') -Raw).Trim()
$actual = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw "mrbones.exe SHA-256 mismatch: $actual" }
foreach ($file in @($manifest, $disc1, $disc2)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing file: $file" }
}
$saveDir = Join-Path $env:LOCALAPPDATA 'MrBonesPort\clean-test-saves'
New-Item -ItemType Directory -Path $saveDir -Force | Out-Null
$env:MB_CONFIG = Join-Path $env:LOCALAPPDATA 'MrBonesPort\clean-test.ini'
$args = @($disc1, $manifest, '--disc2', $disc2, '--save-dir', $saveDir)
switch ($Mode) {
    'check'    { $args += '--check' }
    'lose'     { $args += @('--level', '/28LOSE') }
    'finale'   { $args += @('--level', '/27FINALE') }
    'campaign' { $args += '--full-campaign' }
}
Write-Host "Running $Mode. Saves: $saveDir"
& $exe @args
if ($LASTEXITCODE -ne 0) { throw "mrbones.exe exited with code $LASTEXITCODE" }
'@
$runner = $runner.Replace('__DISC1_CUE__', [IO.Path]::GetFileName($disc1Files[0]))
$runner = $runner.Replace('__DISC2_CUE__', [IO.Path]::GetFileName($disc2Files[0]))
Set-Content -LiteralPath (Join-Path $output 'run_tests.ps1') -Value $runner -Encoding UTF8
$batch = '@echo off' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*' + "`r`n" + 'exit /b %ERRORLEVEL%'
Set-Content -LiteralPath (Join-Path $output 'run_tests.bat') -Value $batch -Encoding ASCII
Write-Host "Package ready: $output"
Write-Host "mrbones.exe SHA-256: $hash"
Write-Host "Direct imports: $($actual -join ', ')"
