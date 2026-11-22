# Opaque-Box E2E Test Suite Runner for MrBones - Sega Saturn (F:\MrBonesPort)
# Usage:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File F:\MrBonesPort\tests\e2e\run_e2e_suite.ps1 [-Tier All|1|2|3|4] [-BuildDir build-static|build|Both] [-FeatureFilter <regex>] [-IdFilter <regex>] [-ListOnly]
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File F:\MrBonesPort\tests\e2e\run_e2e_suite.ps1 ...

[CmdletBinding()]
param(
    [ValidateSet('All', '1', '2', '3', '4')]
    [string]$Tier = 'All',

    [ValidateSet('build-static', 'build', 'Both')]
    [string]$BuildDir = 'build-static',

    [string]$FeatureFilter = '',

    [string]$IdFilter = '',

    [switch]$ListOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Permanent Silent Audio Rule
$env:MB_MUTE = '1'

. (Join-Path $PSScriptRoot 'e2e_helpers.ps1')
. (Join-Path $PSScriptRoot 'tests_tier1.ps1')
. (Join-Path $PSScriptRoot 'tests_tier2.ps1')
. (Join-Path $PSScriptRoot 'tests_tier3.ps1')
. (Join-Path $PSScriptRoot 'tests_tier4.ps1')

$allSpecs = New-Object System.Collections.Generic.List[object]
if ($Tier -eq 'All' -or $Tier -eq '1') {
    foreach ($t in (Get-Tier1Tests)) { [void]$allSpecs.Add($t) }
}
if ($Tier -eq 'All' -or $Tier -eq '2') {
    foreach ($t in (Get-Tier2Tests)) { [void]$allSpecs.Add($t) }
}
if ($Tier -eq 'All' -or $Tier -eq '3') {
    foreach ($t in (Get-Tier3Tests)) { [void]$allSpecs.Add($t) }
}
if ($Tier -eq 'All' -or $Tier -eq '4') {
    foreach ($t in (Get-Tier4Tests)) { [void]$allSpecs.Add($t) }
}

$filtered = New-Object System.Collections.Generic.List[object]
foreach ($spec in $allSpecs) {
    if (-not [string]::IsNullOrEmpty($FeatureFilter) -and ($spec.Feature -notmatch $FeatureFilter)) {
        continue
    }
    if (-not [string]::IsNullOrEmpty($IdFilter) -and ($spec.Id -notmatch $IdFilter)) {
        continue
    }
    [void]$filtered.Add($spec)
}

if ($ListOnly) {
    foreach ($s in $filtered) {
        Write-Host ("[Tier {0}] {1} ({2}): {3}" -f $s.Tier, $s.Id, $s.Feature, $s.Title)
    }
    $t1Count = @($filtered | Where-Object { $_.Tier -eq 1 }).Count
    $t2Count = @($filtered | Where-Object { $_.Tier -eq 2 }).Count
    $t3Count = @($filtered | Where-Object { $_.Tier -eq 3 }).Count
    $t4Count = @($filtered | Where-Object { $_.Tier -eq 4 }).Count
    Write-Host ''
    Write-Host ("Counts -> Tier 1: {0}, Tier 2: {1}, Tier 3: {2}, Tier 4: {3}, Total: {4}" -f $t1Count, $t2Count, $t3Count, $t4Count, $filtered.Count)
    exit 0
}

if ($filtered.Count -eq 0) {
    Write-Error 'No E2E tests matched the specified filters.'
    exit 1
}

$ctx = Initialize-E2EContext -BuildDir $BuildDir
$buildTargets = @(Get-BuildTargetsToRun -BuildDirParam $BuildDir)
$results = New-Object System.Collections.Generic.List[object]
$suiteSw = [System.Diagnostics.Stopwatch]::StartNew()

try {
    Write-Host ("MrBones E2E Suite | RunId={0} | Tier={1} | BuildDir={2} | MB_MUTE={3} | Tests={4} x {5} target(s)" -f `
        $ctx.RunId, $Tier, $BuildDir, $env:MB_MUTE, $filtered.Count, $buildTargets.Count)
    Write-Host ("Isolation TempRoot: {0}" -f $ctx.TempRoot)
    Write-Host ''

    foreach ($target in $buildTargets) {
        if ($buildTargets.Count -gt 1) {
            Write-Host "=== Target Build: $target ==="
        }
        foreach ($spec in $filtered) {
            $res = Invoke-E2ETestCase -Spec $spec -BuildDir $target
            $row = [PSCustomObject]@{
                Id         = $res.Id
                Tier       = $res.Tier
                Feature    = $res.Feature
                BuildDir   = $target
                Title      = $res.Title
                Status     = $res.Status
                DurationMs = $res.DurationMs
                Detail     = $res.Detail
            }
            [void]$results.Add($row)
            Write-Host ("[{0}] [{1}] {2} ({3}) {4}ms - {5} | {6}" -f `
                $row.Status, $target, $row.Id, $row.Feature, $row.DurationMs, $row.Title, $row.Detail)
        }
        Write-Host ''
    }

    $suiteSw.Stop()

    Write-Host '--- Tier Summary ---'
    $tierStats = @()
    foreach ($tNum in @(1, 2, 3, 4)) {
        $sub = @($results | Where-Object { $_.Tier -eq $tNum })
        if ($sub.Count -eq 0) { continue }
        $passN = @($sub | Where-Object { $_.Status -eq 'PASS' }).Count
        $failN = @($sub | Where-Object { $_.Status -eq 'FAIL' }).Count
        $durN = 0L
        foreach ($x in $sub) { $durN += $x.DurationMs }
        Write-Host ("Tier {0}: {1}/{2} PASS, {3} FAIL ({4} ms)" -f $tNum, $passN, $sub.Count, $failN, $durN)
        $tierStats += [PSCustomObject]@{
            Tier       = $tNum
            Total      = $sub.Count
            Passed     = $passN
            Failed     = $failN
            DurationMs = $durN
        }
    }

    $totalPass = @($results | Where-Object { $_.Status -eq 'PASS' }).Count
    $totalFail = @($results | Where-Object { $_.Status -eq 'FAIL' }).Count
    Write-Host ("TOTAL : {0}/{1} PASS, {2} FAIL ({3} ms)" -f $totalPass, $results.Count, $totalFail, $suiteSw.ElapsedMilliseconds)

    # Clean up scratch save/capture/wd subdirectories inside TempRoot before writing summary
    Get-ChildItem -LiteralPath $ctx.TempRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $ctx.EmptyIni) {
        Remove-Item -LiteralPath $ctx.EmptyIni -Force -ErrorAction SilentlyContinue
    }

    $summaryObj = [PSCustomObject]@{
        RunId           = $ctx.RunId
        TimestampUtc    = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        TierFilter      = $Tier
        BuildDir        = $BuildDir
        FeatureFilter   = $FeatureFilter
        IdFilter        = $IdFilter
        TotalExecuted   = $results.Count
        TotalPassed     = $totalPass
        TotalFailed     = $totalFail
        TotalDurationMs = $suiteSw.ElapsedMilliseconds
        TierSummary     = $tierStats
        Results         = $results.ToArray()
    }
    $summaryJsonPath = Join-Path $ctx.TempRoot 'summary.json'
    $summaryJson = $summaryObj | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($summaryJsonPath, $summaryJson, [System.Text.UTF8Encoding]::new($false))
    Write-Host ("Summary JSON: {0}" -f $summaryJsonPath)

    if ($totalFail -gt 0) {
        exit 1
    }
    exit 0
} finally {
    # Ensure any scratch subdirs are cleaned even on abort, while preserving summary.json if created
    if ($null -ne $ctx -and (Test-Path -LiteralPath $ctx.TempRoot)) {
        Get-ChildItem -LiteralPath $ctx.TempRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
