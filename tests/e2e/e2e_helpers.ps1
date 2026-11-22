# Shared helpers for MrBonesPort opaque-box E2E test suite (Tiers 1-4)
# Compatible with Windows PowerShell 5.1 and PowerShell 7+.
# Permanent Audio Rule: Every binary/CTest execution sets MB_MUTE=1 and --mute where supported.

Set-StrictMode -Version Latest

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:Disc1Cue = Join-Path $script:RepoRoot 'disco1\Mr. Bones (USA) (Disc 1).cue'
$script:Disc2Cue = Join-Path $script:RepoRoot 'disco2\Mr. Bones (USA) (Disc 2).cue'
$script:ManifestPath = Join-Path $script:RepoRoot 'manifest\versions.txt'
$script:LogPath = Join-Path $script:RepoRoot 'LOG-DESENVOLVIMENTO.md'
$script:GitIgnorePath = Join-Path $script:RepoRoot '.gitignore'
$script:HostAudioPath = Join-Path $script:RepoRoot 'src\platform\host_audio.c'
$script:CMakeListsPath = Join-Path $script:RepoRoot 'CMakeLists.txt'
$script:SidescrlTestPath = Join-Path $script:RepoRoot 'tests\test_sidescrl_phase.c'
$script:HandoffChatGptPath = Join-Path $script:RepoRoot 'docs\handoff-chatgpt-special-runway.md'

$script:E2EContext = $null

function Initialize-E2EContext {
    param(
        [string]$BuildDir = 'build-static'
    )
    $runId = ([DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss')) + '_' + ([System.Guid]::NewGuid().ToString('N').Substring(0, 8))
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('mrbones_e2e_' + $runId)
    [System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null
    $emptyIni = Join-Path $tempRoot 'empty.ini'
    [System.IO.File]::WriteAllText($emptyIni, '', [System.Text.UTF8Encoding]::new($false))

    $script:E2EContext = [PSCustomObject]@{
        RunId           = $runId
        TempRoot        = $tempRoot
        EmptyIni        = $emptyIni
        BuildDir        = $BuildDir
        CTestCache      = @{}
        CTestJsonCache  = @{}
        MovieAudioCache = @{}
        MrBonesCache    = @{}
    }
    return $script:E2EContext
}

function Remove-E2EContext {
    if ($null -ne $script:E2EContext -and (Test-Path -LiteralPath $script:E2EContext.TempRoot)) {
        try {
            Remove-Item -LiteralPath $script:E2EContext.TempRoot -Recurse -Force -ErrorAction SilentlyContinue
        } catch {
            # Ignore transient file lock cleanup issues
        }
    }
}

function New-E2ETempSubdir {
    param(
        [string]$Prefix = 'case'
    )
    if ($null -eq $script:E2EContext) {
        Initialize-E2EContext | Out-Null
    }
    $sub = Join-Path $script:E2EContext.TempRoot ($Prefix + '_' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8))
    [System.IO.Directory]::CreateDirectory($sub) | Out-Null
    return $sub
}

function Get-BuildConfigName {
    param([string]$BuildDir)
    if ($BuildDir -eq 'build') { return 'Debug' }
    return 'Release'
}

function Get-BuildTargetsToRun {
    param([string]$BuildDirParam)
    if ($BuildDirParam -eq 'Both') {
        return @('build-static', 'build')
    }
    return @($BuildDirParam)
}

function Get-BuildBinaryPath {
    param(
        [string]$BuildDir,
        [string]$BinaryName
    )
    return Join-Path (Join-Path $script:RepoRoot $BuildDir) $BinaryName
}

function Format-Win32Arg {
    param([string]$Arg)
    if ($null -eq $Arg) { return '""' }
    if ($Arg.Length -eq 0) { return '""' }
    if ($Arg -notmatch '[\s"]') { return $Arg }
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    $backslashes = 0
    for ($i = 0; $i -lt $Arg.Length; $i++) {
        $ch = $Arg[$i]
        if ($ch -eq '\') {
            $backslashes++
        } elseif ($ch -eq '"') {
            [void]$sb.Append(('\' * ($backslashes * 2 + 1)))
            [void]$sb.Append('"')
            $backslashes = 0
        } else {
            if ($backslashes -gt 0) {
                [void]$sb.Append(('\' * $backslashes))
                $backslashes = 0
            }
            [void]$sb.Append($ch)
        }
    }
    if ($backslashes -gt 0) {
        [void]$sb.Append(('\' * ($backslashes * 2)))
    }
    [void]$sb.Append('"')
    return $sb.ToString()
}

function Format-Win32ArgList {
    param([string[]]$ArgList)
    if ($null -eq $ArgList -or $ArgList.Count -eq 0) { return '' }
    $parts = @()
    foreach ($a in $ArgList) {
        $parts += (Format-Win32Arg -Arg $a)
    }
    return ($parts -join ' ')
}

function Invoke-IsolatedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = '',
        [hashtable]$EnvOverrides = @{},
        [int]$TimeoutSec = 120,
        [switch]$AllowNonMuteEnv
    )
    if ($null -eq $script:E2EContext) {
        Initialize-E2EContext | Out-Null
    }
    if ([string]::IsNullOrEmpty($WorkingDirectory)) {
        $WorkingDirectory = New-E2ETempSubdir -Prefix 'wd'
    } elseif (-not (Test-Path -LiteralPath $WorkingDirectory)) {
        [System.IO.Directory]::CreateDirectory($WorkingDirectory) | Out-Null
    }

    $resolvedPath = $FilePath
    if (-not (Test-Path -LiteralPath $resolvedPath)) {
        $cmd = Get-Command -Name $FilePath -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $cmd -and -not [string]::IsNullOrEmpty($cmd.Source)) {
            $resolvedPath = $cmd.Source
        } else {
            return [PSCustomObject]@{
                ExitCode = -999
                StdOut   = ''
                StdErr   = "Binary not found: $FilePath"
                TimedOut = $false
                Command  = $FilePath
            }
        }
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $resolvedPath
    $psi.Arguments = Format-Win32ArgList -ArgList $Arguments
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    # Enforce permanent silent audio & config isolation
    $psi.EnvironmentVariables['MB_MUTE'] = '1'
    $psi.EnvironmentVariables['MB_CONFIG'] = $script:E2EContext.EmptyIni
    $psi.EnvironmentVariables['MB_DISC1'] = ''
    $psi.EnvironmentVariables['MB_DISC2'] = ''
    $psi.EnvironmentVariables['MB_MANIFEST'] = ''
    $psi.EnvironmentVariables['MB_SAVE_DIR'] = ''
    $psi.EnvironmentVariables['MB_DATA_DIR'] = ''

    foreach ($k in $EnvOverrides.Keys) {
        $val = [string]$EnvOverrides[$k]
        if ($k -eq 'MB_MUTE' -and -not $AllowNonMuteEnv) {
            if ($val -notin @('1', 'on', 'ON', 'true', 'TRUE', 'yes', 'YES', 'y', 'Y', 't', 'T')) {
                throw "Safety guard: Refusing to run process with unmuted MB_MUTE='$val'"
            }
        }
        $psi.EnvironmentVariables[$k] = $val
    }

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $timedOut = $false
    $stdout = ''
    $stderr = ''
    $exitCode = -1

    try {
        [void]$proc.Start()
        $outTask = $proc.StandardOutput.ReadToEndAsync()
        $errTask = $proc.StandardError.ReadToEndAsync()
        $exited = $proc.WaitForExit($TimeoutSec * 1000)
        if (-not $exited) {
            $timedOut = $true
            try { $proc.Kill() } catch {}
            try { $proc.WaitForExit(3000) | Out-Null } catch {}
        } else {
            # Ensure async readers flush
            $proc.WaitForExit()
        }
        try {
            if ($outTask.Wait(3000)) { $stdout = $outTask.Result }
        } catch {}
        try {
            if ($errTask.Wait(3000)) { $stderr = $errTask.Result }
        } catch {}
        if (-not $timedOut) {
            $exitCode = $proc.ExitCode
        }
    } finally {
        $proc.Dispose()
    }

    return [PSCustomObject]@{
        ExitCode = $exitCode
        StdOut   = $stdout
        StdErr   = $stderr
        TimedOut = $timedOut
        Command  = "$FilePath $($psi.Arguments)"
    }
}

function Invoke-MrBones {
    param(
        [string]$BuildDir = 'build-static',
        [string[]]$ArgList = @(),
        [string]$SaveDir = '',
        [string]$CaptureDir = '',
        [string]$WorkingDirectory = '',
        [hashtable]$EnvOverrides = @{},
        [int]$TimeoutSec = 120,
        [switch]$OmitSaveDir,
        [switch]$OmitMuteFlag
    )
    $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'mrbones.exe'
    $finalArgs = New-Object System.Collections.Generic.List[string]
    foreach ($a in $ArgList) { [void]$finalArgs.Add($a) }

    if (-not $OmitMuteFlag -and ($finalArgs -notcontains '--mute')) {
        [void]$finalArgs.Add('--mute')
    }
    if (-not $OmitSaveDir -and ($finalArgs -notcontains '--save-dir')) {
        if ([string]::IsNullOrEmpty($SaveDir)) {
            $SaveDir = New-E2ETempSubdir -Prefix 'save'
        } elseif (-not (Test-Path -LiteralPath $SaveDir)) {
            [System.IO.Directory]::CreateDirectory($SaveDir) | Out-Null
        }
        [void]$finalArgs.Add('--save-dir')
        [void]$finalArgs.Add($SaveDir)
    }
    if (-not [string]::IsNullOrEmpty($CaptureDir) -and ($finalArgs -notcontains '--capture-dir')) {
        if (-not (Test-Path -LiteralPath $CaptureDir)) {
            [System.IO.Directory]::CreateDirectory($CaptureDir) | Out-Null
        }
        [void]$finalArgs.Add('--capture-dir')
        [void]$finalArgs.Add($CaptureDir)
    }

    $res = Invoke-IsolatedProcess -FilePath $exe -Arguments ($finalArgs.ToArray()) `
        -WorkingDirectory $WorkingDirectory -EnvOverrides $EnvOverrides -TimeoutSec $TimeoutSec
    return [PSCustomObject]@{
        ExitCode   = $res.ExitCode
        StdOut     = $res.StdOut
        StdErr     = $res.StdErr
        TimedOut   = $res.TimedOut
        Command    = $res.Command
        SaveDir    = $SaveDir
        CaptureDir = $CaptureDir
    }
}

function Invoke-MbVerify {
    param(
        [string]$BuildDir = 'build-static',
        [string[]]$ArgList = @(),
        [hashtable]$EnvOverrides = @{},
        [int]$TimeoutSec = 60
    )
    $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'mbverify.exe'
    return Invoke-IsolatedProcess -FilePath $exe -Arguments $ArgList -EnvOverrides $EnvOverrides -TimeoutSec $TimeoutSec
}

function Invoke-CTest {
    param(
        [string]$BuildDir = 'build-static',
        [string]$RegexFilter = '',
        [int]$TimeoutSec = 300,
        [switch]$UseCache
    )
    if ($null -eq $script:E2EContext) {
        Initialize-E2EContext | Out-Null
    }
    $cacheKey = "$BuildDir|$RegexFilter"
    if ($UseCache -and $script:E2EContext.CTestCache.ContainsKey($cacheKey)) {
        return $script:E2EContext.CTestCache[$cacheKey]
    }
    $testDir = Join-Path $script:RepoRoot $BuildDir
    $cfg = Get-BuildConfigName -BuildDir $BuildDir
    $ctestArgs = @('--test-dir', $testDir, '-C', $cfg, '--output-on-failure')
    if (-not [string]::IsNullOrEmpty($RegexFilter)) {
        $ctestArgs += @('-R', $RegexFilter, '-V')
    }
    $res = Invoke-IsolatedProcess -FilePath 'ctest.exe' -Arguments $ctestArgs `
        -WorkingDirectory $testDir -TimeoutSec $TimeoutSec
    if ($UseCache) {
        $script:E2EContext.CTestCache[$cacheKey] = $res
    }
    return $res
}

function Test-CTestAllPassed {
    param([Parameter(Mandatory = $true)]$CTestResult)
    if ($CTestResult.TimedOut -or $CTestResult.ExitCode -ne 0) {
        return [PSCustomObject]@{ Passed = $false; Count = 0; Detail = "exit=$($CTestResult.ExitCode) timeout=$($CTestResult.TimedOut)" }
    }
    if ($CTestResult.StdOut -match '100% tests passed(?:, 0 tests failed)? out of (\d+)') {
        return [PSCustomObject]@{ Passed = $true; Count = [int]$Matches[1]; Detail = "$($Matches[1])/$($Matches[1]) passed" }
    }
    return [PSCustomObject]@{ Passed = $false; Count = 0; Detail = 'CTest output missing 100% tests passed summary' }
}

function Invoke-MovieAudio {
    param(
        [string]$BuildDir = 'build-static',
        [string]$MuteValue = '1',
        [switch]$UseCache
    )
    if ($null -eq $script:E2EContext) {
        Initialize-E2EContext | Out-Null
    }
    $cacheKey = "$BuildDir|$MuteValue"
    if ($UseCache -and $script:E2EContext.MovieAudioCache.ContainsKey($cacheKey)) {
        return $script:E2EContext.MovieAudioCache[$cacheKey]
    }
    $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'test_movie_audio.exe'
    $wd = New-E2ETempSubdir -Prefix ('movie_audio_' + $MuteValue)
    $res = Invoke-IsolatedProcess -FilePath $exe -WorkingDirectory $wd -EnvOverrides @{
        MB_MUTE  = $MuteValue
        MB_DISC1 = $script:Disc1Cue
        MB_DISC2 = $script:Disc2Cue
    } -TimeoutSec 120
    $wrapped = [PSCustomObject]@{
        ExitCode              = $res.ExitCode
        StdOut                = $res.StdOut
        StdErr                = $res.StdErr
        TimedOut              = $res.TimedOut
        WorkingDirectory      = $wd
        CreatedAudioRefInWd   = (Test-Path -LiteralPath (Join-Path $wd 'test_audio_reference'))
        CreatedAudioRefInRoot = (Test-Path -LiteralPath (Join-Path $script:RepoRoot 'test_audio_reference'))
    }
    if ($UseCache) {
        $script:E2EContext.MovieAudioCache[$cacheKey] = $wrapped
    }
    return $wrapped
}

function Get-CTestMetadata {
    param(
        [string]$BuildDir = 'build-static'
    )
    if ($null -eq $script:E2EContext) {
        Initialize-E2EContext | Out-Null
    }
    if ($script:E2EContext.CTestJsonCache.ContainsKey($BuildDir)) {
        return $script:E2EContext.CTestJsonCache[$BuildDir]
    }
    $testDir = Join-Path $script:RepoRoot $BuildDir
    $res = Invoke-IsolatedProcess -FilePath 'ctest.exe' -Arguments @('--test-dir', $testDir, '--show-only=json-v1') `
        -WorkingDirectory $testDir -TimeoutSec 30
    if ($res.ExitCode -ne 0) {
        throw "ctest --show-only=json-v1 failed for $BuildDir (exit $($res.ExitCode)): $($res.StdErr)"
    }
    $parsed = $res.StdOut | ConvertFrom-Json
    $script:E2EContext.CTestJsonCache[$BuildDir] = $parsed
    return $parsed
}

function Get-CTestPropertyMap {
    param([Parameter(Mandatory = $true)]$TestEntry)
    $map = @{}
    if ($null -ne $TestEntry.properties) {
        foreach ($p in $TestEntry.properties) {
            $map[$p.name] = $p.value
        }
    }
    return $map
}

function Invoke-GitReadOnly {
    param(
        [Parameter(Mandatory = $true)][string[]]$ArgList,
        [int]$TimeoutSec = 30
    )
    if ($ArgList.Count -eq 0) { throw 'Invoke-GitReadOnly requires arguments' }
    $allowed = @('status', 'check-ignore', 'ls-files', 'ls-tree', 'rev-parse', 'log', 'remote', 'config', 'branch', 'show', 'fsck')
    $sub = $ArgList[0]
    if ($allowed -notcontains $sub) {
        throw "Refusing non-read-only git subcommand: $sub"
    }
    if ($sub -eq 'config' -and ($ArgList -notcontains '--get') -and ($ArgList -notcontains '--list') -and ($ArgList -notcontains '-l')) {
        throw 'Invoke-GitReadOnly only allows read-only git config queries'
    }
    return Invoke-IsolatedProcess -FilePath 'git.exe' -Arguments $ArgList `
        -WorkingDirectory $script:RepoRoot -TimeoutSec $TimeoutSec
}

function Test-PpmP6File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$ExpectedWidth = 0,
        [int]$ExpectedHeight = 0
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Missing file: $Path" }
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 12) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Too small ($($bytes.Length) B)" }
    }

    # Parse 4 ASCII header tokens (magic, width, height, maxval) skipping # comments
    $tokens = New-Object System.Collections.Generic.List[string]
    $idx = 0
    $len = $bytes.Length
    while ($tokens.Count -lt 4 -and $idx -lt $len) {
        # Skip whitespace
        while ($idx -lt $len -and ($bytes[$idx] -eq 32 -or $bytes[$idx] -eq 9 -or $bytes[$idx] -eq 10 -or $bytes[$idx] -eq 13)) {
            $idx++
        }
        if ($idx -ge $len) { break }
        if ($bytes[$idx] -eq 35) {
            while ($idx -lt $len -and $bytes[$idx] -ne 10) { $idx++ }
            continue
        }
        $sb = New-Object System.Text.StringBuilder
        while ($idx -lt $len -and $bytes[$idx] -ne 32 -and $bytes[$idx] -ne 9 -and $bytes[$idx] -ne 10 -and $bytes[$idx] -ne 13) {
            if ($bytes[$idx] -eq 35) { break }
            [void]$sb.Append([char]$bytes[$idx])
            $idx++
        }
        if ($sb.Length -gt 0) { [void]$tokens.Add($sb.ToString()) }
    }
    if ($tokens.Count -lt 4) {
        return [PSCustomObject]@{ Valid = $false; Reason = 'Incomplete PPM header' }
    }
    # Consume single whitespace byte after maxval
    if ($idx -lt $len) { $idx++ }

    $magic = $tokens[0]
    if ($magic -ne 'P6') {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected P6, got '$magic'" }
    }
    $w = [int]$tokens[1]
    $h = [int]$tokens[2]
    $maxv = [int]$tokens[3]
    if ($maxv -ne 255) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected maxval 255, got $maxv" }
    }
    if ($ExpectedWidth -gt 0 -and $w -ne $ExpectedWidth) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected width $ExpectedWidth, got $w" }
    }
    if ($ExpectedHeight -gt 0 -and $h -ne $ExpectedHeight) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected height $ExpectedHeight, got $h" }
    }
    $expectedPayload = $w * $h * 3
    $actualPayload = $bytes.Length - $idx
    if ($actualPayload -ne $expectedPayload) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Payload mismatch: got $actualPayload, expected $expectedPayload" }
    }

    $nonBlack = 0
    $colorSet = New-Object 'System.Collections.Generic.HashSet[int]'
    $sampleCount = 0
    for ($p = $idx; $p + 2 -lt $bytes.Length; $p += 3) {
        $r = [int]$bytes[$p]
        $g = [int]$bytes[$p + 1]
        $b = [int]$bytes[$p + 2]
        if ($r -ne 0 -or $g -ne 0 -or $b -ne 0) {
            $nonBlack++
        }
        if ($colorSet.Count -lt 64) {
            [void]$colorSet.Add(($r -shl 16) -bor ($g -shl 8) -bor $b)
        }
        $sampleCount++
    }

    return [PSCustomObject]@{
        Valid         = $true
        Width         = $w
        Height        = $h
        PixelCount    = $sampleCount
        NonBlackCount = $nonBlack
        UniqueSample  = $colorSet.Count
        Reason        = 'OK'
    }
}

function Test-NvmShadowFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Missing nvmshadow.bin at $Path" }
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ne 40) {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected 40 bytes, got $($bytes.Length)" }
    }
    $tag = [System.Text.Encoding]::ASCII.GetString($bytes, 0x24, 4)
    if ($tag -ne 'BONE') {
        return [PSCustomObject]@{ Valid = $false; Reason = "Expected BONE tag at +0x24, got '$tag'" }
    }
    $levelWord = ([uint32]$bytes[0x1C] -shl 24) -bor ([uint32]$bytes[0x1D] -shl 16) -bor ([uint32]$bytes[0x1E] -shl 8) -bor ([uint32]$bytes[0x1F])
    $extraWord = ([uint32]$bytes[0x20] -shl 24) -bor ([uint32]$bytes[0x21] -shl 16) -bor ([uint32]$bytes[0x22] -shl 8) -bor ([uint32]$bytes[0x23])
    $completed = [int]$bytes[1]
    $unlocks = @()
    for ($i = 0; $i -lt 24; $i++) {
        $unlocks += [int]$bytes[4 + $i]
    }
    return [PSCustomObject]@{
        Valid     = $true
        Tag       = $tag
        Completed = $completed
        LevelWord = $levelWord
        ExtraWord = $extraWord
        Unlocks   = $unlocks
        Reason    = 'OK'
    }
}

function Test-BenchmarkCsvOutput {
    param(
        [Parameter(Mandatory = $true)][string]$StdOut,
        [Parameter(Mandatory = $true)][int]$ExpectedFrames
    )
    $lines = @($StdOut -split "`r?`n" | Where-Object { $_ -match '^frame_time,' })
    if ($lines.Count -ne $ExpectedFrames) {
        return [PSCustomObject]@{
            Valid  = $false
            Count  = $lines.Count
            Reason = "Expected $ExpectedFrames frame_time lines, got $($lines.Count)"
        }
    }
    $idx = 1
    foreach ($ln in $lines) {
        $parts = $ln.Split(',')
        if ($parts.Length -ne 6) {
            return [PSCustomObject]@{ Valid = $false; Count = $lines.Count; Reason = "Malformed CSV line: $ln" }
        }
        $n = 0
        if (-not [int]::TryParse($parts[1], [ref]$n) -or $n -ne $idx) {
            return [PSCustomObject]@{ Valid = $false; Count = $lines.Count; Reason = "Expected frame index $idx, got '$($parts[1])'" }
        }
        $workMs = 0.0
        $intervalMs = 0.0
        if (-not [double]::TryParse($parts[2], [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$workMs) -or
            -not [double]::TryParse($parts[3], [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$intervalMs)) {
            return [PSCustomObject]@{ Valid = $false; Count = $lines.Count; Reason = "Non-numeric timing in: $ln" }
        }
        if ([double]::IsNaN($workMs) -or [double]::IsInfinity($workMs) -or $workMs -lt 0.0 -or
            [double]::IsNaN($intervalMs) -or [double]::IsInfinity($intervalMs) -or $intervalMs -lt 0.0) {
            return [PSCustomObject]@{ Valid = $false; Count = $lines.Count; Reason = "Invalid timing value in: $ln" }
        }
        $idx++
    }
    return [PSCustomObject]@{ Valid = $true; Count = $lines.Count; Reason = 'OK' }
}

function Get-HostAudioAnalysis {
    if (-not (Test-Path -LiteralPath $script:HostAudioPath)) {
        return [PSCustomObject]@{ Exists = $false; Reason = 'Missing src/platform/host_audio.c' }
    }
    $src = [System.IO.File]::ReadAllText($script:HostAudioPath, [System.Text.Encoding]::UTF8)
    $isMutedBlock = ''
    if ($src -match '(?s)static\s+int\s+is_muted\s*\([^)]*\)\s*\{(.*?)\n\}') {
        $isMutedBlock = $Matches[1]
    }
    $pcm8ThreadBlock = ''
    if ($src -match '(?s)static\s+DWORD\s+WINAPI\s+pcm8_thread\s*\([^)]*\)\s*\{(.*?)\n\}') {
        $pcm8ThreadBlock = $Matches[1]
    }
    $supportsOn = ($isMutedBlock -match '(?i)"on"|_stricmp.*on|''o''.*''n''')
    $pcm8Guarded = ($pcm8ThreadBlock -match 'is_muted\s*\(\s*\)')
    # Ensure naive env[0] == 'o' without checking second char isn't present (which would treat "off" as muted)
    $naiveO = ($isMutedBlock -match "env\[0\]\s*==\s*'o'" -and -not ($isMutedBlock -match "env\[1\]"))
    return [PSCustomObject]@{
        Exists      = $true
        SupportsOn  = $supportsOn
        Pcm8Guarded = $pcm8Guarded
        NaiveO      = $naiveO
        SourceMtime = (Get-Item -LiteralPath $script:HostAudioPath).LastWriteTimeUtc
    }
}

function New-E2EResult {
    param(
        [string]$Id,
        [int]$Tier,
        [string]$Feature,
        [string]$Title,
        [string]$Status,
        [long]$DurationMs,
        [string]$Detail
    )
    return [PSCustomObject]@{
        Id         = $Id
        Tier       = $Tier
        Feature    = $Feature
        Title      = $Title
        Status     = $Status
        DurationMs = $DurationMs
        Detail     = $Detail
    }
}

function New-E2ETestSpec {
    param(
        [string]$Id,
        [int]$Tier,
        [string]$Feature,
        [string]$Title,
        [scriptblock]$Body
    )
    return [PSCustomObject]@{
        Id      = $Id
        Tier    = $Tier
        Feature = $Feature
        Title   = $Title
        Body    = $Body
    }
}

function Invoke-E2ETestCase {
    param(
        [Parameter(Mandatory = $true)]$Spec,
        [Parameter(Mandatory = $true)][string]$BuildDir
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $status = 'FAIL'
    $detail = ''
    try {
        $out = & $Spec.Body $BuildDir
        if ($null -eq $out) {
            $status = 'FAIL'
            $detail = 'Test body returned null'
        } elseif ($out -is [bool]) {
            if ($out) { $status = 'PASS'; $detail = 'OK' } else { $status = 'FAIL'; $detail = 'Assertion returned false' }
        } elseif ($out.PSObject.Properties['Passed']) {
            if ($out.Passed) { $status = 'PASS' } else { $status = 'FAIL' }
            if ($out.PSObject.Properties['Detail'] -and -not [string]::IsNullOrEmpty($out.Detail)) {
                $detail = [string]$out.Detail
            } else {
                $detail = if ($out.Passed) { 'OK' } else { 'Failed' }
            }
        } else {
            $status = 'FAIL'
            $detail = "Unexpected test return type: $($out.GetType().Name)"
        }
    } catch {
        $status = 'FAIL'
        $detail = "Exception: $($_.Exception.Message)"
    }
    $sw.Stop()
    return New-E2EResult -Id $Spec.Id -Tier $Spec.Tier -Feature $Spec.Feature -Title $Spec.Title `
        -Status $status -DurationMs $sw.ElapsedMilliseconds -Detail $detail
}

function Assert-TestSidescrlLiveAndPattern {
    param(
        [string]$BuildDir,
        [string]$RequiredOutputRegex,
        [string]$RequiredSourceRegex,
        [string]$StageLabel
    )
    $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^test_sidescrl_phase$' -UseCache
    if ($ct.ExitCode -ne 0) {
        return [PSCustomObject]@{ Passed = $false; Detail = "test_sidescrl_phase failed (exit $($ct.ExitCode))" }
    }
    if ($ct.StdOut -notmatch 'OK sidescrl_phase') {
        return [PSCustomObject]@{ Passed = $false; Detail = 'test_sidescrl_phase output missing OK sidescrl_phase' }
    }
    if (-not [string]::IsNullOrEmpty($RequiredOutputRegex) -and ($ct.StdOut -notmatch $RequiredOutputRegex)) {
        return [PSCustomObject]@{ Passed = $false; Detail = "$StageLabel output missing pattern: $RequiredOutputRegex" }
    }
    if (-not [string]::IsNullOrEmpty($RequiredSourceRegex)) {
        if (-not (Test-Path -LiteralPath $script:SidescrlTestPath)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing tests/test_sidescrl_phase.c' }
        }
        $src = [System.IO.File]::ReadAllText($script:SidescrlTestPath, [System.Text.Encoding]::UTF8)
        if ($src -notmatch $RequiredSourceRegex) {
            return [PSCustomObject]@{ Passed = $false; Detail = "$StageLabel Segment A/B/C verification missing in test_sidescrl_phase.c (pattern: $RequiredSourceRegex)" }
        }
    }
    return [PSCustomObject]@{ Passed = $true; Detail = "$StageLabel CTest & regression assertions verified" }
}
