# Tier 4 Realistic End-User Application Scenarios (7 scenarios: T4-S01..T4-S07)

function Get-Tier4Tests {
    $tests = New-Object System.Collections.Generic.List[object]

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S01' -Tier 4 -Feature 'F3+F4-F11+F14' -Title 'Scenario 1: First-time disc verification (mbverify --quick) & silent full campaign' -Body {
        param($BuildDir)
        $mv = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--disc1', $script:Disc1Cue, '--disc2', $script:Disc2Cue, '--manifest', $script:ManifestPath, '--quick')
        if ($mv.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "mbverify failed (exit $($mv.ExitCode))" }
        }
        $saveDir = New-E2ETempSubdir -Prefix 's01_campaign'
        $fc = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--full-campaign') -SaveDir $saveDir -TimeoutSec 300
        if ($fc.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "full-campaign failed (exit $($fc.ExitCode)): $($fc.StdErr.Trim())" }
        }
        if ($fc.StdOut -notmatch 'full-campaign: MENU.+stages.+INSERTCD.+FINALE.+MENU OK') {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing full-campaign completion banner' }
        }
        $nvm = Test-NvmShadowFile -Path (Join-Path $saveDir 'nvmshadow.bin')
        if (-not $nvm.Valid -or $nvm.Completed -ne 1 -or $nvm.LevelWord -ne 3) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Invalid campaign nvmshadow: $($nvm.Reason) completed=$($nvm.Completed) levelWord=$($nvm.LevelWord)" }
        }
        for ($i = 0; $i -lt 24; $i++) {
            $want = if ($i -eq 8 -or $i -eq 9) { 0 } else { 1 }
            if ($nvm.Unlocks[$i] -ne $want) {
                return [PSCustomObject]@{ Passed = $false; Detail = "Unexpected unlock[$i]=$($nvm.Unlocks[$i]) (expected $want)" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Disc verify + 22-stage full campaign + BONE nvmshadow verified' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S02' -Tier 4 -Feature 'F3+F4+F7+F8+F10+F11+F14' -Title 'Scenario 2: Full FMV & negative regression check gate (--check --mute)' -Body {
        param($BuildDir)
        $saveDir = New-E2ETempSubdir -Prefix 's02_check'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--check') -SaveDir $saveDir -TimeoutSec 300
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $checks = @(
            'check: corrupt MENU movie rejection OK',
            'check: 13 FMV transitions OK',
            'check: FINALE arena CD-DA -> ending PCM OK',
            'check: corrupt LOSE movie rejection OK',
            'host 1\.0\.0 DirNameTable'
        )
        foreach ($pat in $checks) {
            if ($r.StdOut -notmatch $pat) {
                return [PSCustomObject]@{ Passed = $false; Detail = "Missing check output pattern: $pat" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All --check FMV, audio-switch, and corrupt-movie gates passed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S03' -Tier 4 -Feature 'F3+F4-F11+F14' -Title 'Scenario 3: Multi-stage F4-F11 benchmark (60 frames) & PPM capture audit' -Body {
        param($BuildDir)
        $stages = @(
            @{ Iso = '/06VALLEY'; Disc2 = $false; Tag = '06VALLEY'; MinNonBlack = 100 },
            @{ Iso = '/08BAT1';   Disc2 = $false; Tag = '08BAT1';   MinNonBlack = 100 },
            @{ Iso = '/05MAUSO';  Disc2 = $false; Tag = '05MAUSO';  MinNonBlack = 100 },
            @{ Iso = '/19VILLAGE';Disc2 = $true;  Tag = '19VILLAGE';MinNonBlack = 100 },
            @{ Iso = '/26LAFF';   Disc2 = $true;  Tag = '26LAFF';   MinNonBlack = 100 },
            @{ Iso = '/15GLASS';  Disc2 = $false; Tag = '15GLASS';  MinNonBlack = 1000 },
            @{ Iso = '/17VORTEX'; Disc2 = $false; Tag = '17VORTEX'; MinNonBlack = 100 },
            @{ Iso = '/22SHADOW'; Disc2 = $true;  Tag = '22SHADOW'; MinNonBlack = 100 }
        )
        $capRoot = New-E2ETempSubdir -Prefix 's03_caps'
        foreach ($st in $stages) {
            $baseArgs = @($script:Disc1Cue, $script:ManifestPath)
            if ($st.Disc2) { $baseArgs += @('--disc2', $script:Disc2Cue) }
            $benchArgs = $baseArgs + @('--level', $st.Iso, '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
            $rb = Invoke-MrBones -BuildDir $BuildDir -ArgList $benchArgs -TimeoutSec 120
            if ($rb.ExitCode -ne 0) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$($st.Iso) benchmark failed (exit $($rb.ExitCode))" }
            }
            $csv = Test-BenchmarkCsvOutput -StdOut $rb.StdOut -ExpectedFrames 60
            if (-not $csv.Valid) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$($st.Iso) CSV invalid: $($csv.Reason)" }
            }

            $capArgs = $baseArgs + @('--level', $st.Iso, '--autoplay', '--no-hd-bones', '--scale', '1')
            $rc = Invoke-MrBones -BuildDir $BuildDir -ArgList $capArgs -CaptureDir $capRoot -TimeoutSec 120
            if ($rc.ExitCode -ne 0) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$($st.Iso) capture autoplay failed (exit $($rc.ExitCode))" }
            }
            $ppms = @(Get-ChildItem -LiteralPath $capRoot -Filter "*_$($st.Tag).ppm" -File)
            if ($ppms.Count -lt 1) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$($st.Iso) produced no PPM file" }
            }
            $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
            if (-not $p.Valid -or $p.NonBlackCount -lt $st.MinNonBlack) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$($st.Iso) PPM check failed ($($p.Reason), nonBlack=$($p.NonBlackCount))" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 8 stages F4-F11 passed 60-frame benchmarks & 320x224 PPM capture audit' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S04' -Tier 4 -Feature 'F4-F11+F14' -Title 'Scenario 4: Cross-disc save-state resume (nvmshadow.bin across Disc 1 -> Disc 2)' -Body {
        param($BuildDir)
        $isoWd = New-E2ETempSubdir -Prefix 's04_prog'
        $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'test_stage_progress.exe'
        $rp = Invoke-IsolatedProcess -FilePath $exe -WorkingDirectory $isoWd -EnvOverrides @{
            MB_MUTE     = '1'
            MB_DISC1    = $script:Disc1Cue
            MB_DISC2    = $script:Disc2Cue
            MB_MANIFEST = $script:ManifestPath
        } -TimeoutSec 180
        if ($rp.ExitCode -ne 0 -or $rp.StdOut -notmatch 'OK stage_progress' -or $rp.StdOut -notmatch 'nvm markers intact across VORTEX->RIDE') {
            return [PSCustomObject]@{ Passed = $false; Detail = "test_stage_progress failed (exit $($rp.ExitCode))" }
        }
        $sharedSave = New-E2ETempSubdir -Prefix 's04_shared'
        $r1 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/06VALLEY', '--autoplay', '--no-hd-bones') -SaveDir $sharedSave
        $r2 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/19VILLAGE', '--autoplay', '--no-hd-bones') -SaveDir $sharedSave
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Stage resume failed: d1=$($r1.ExitCode) d2=$($r2.ExitCode)" }
        }
        $nvm = Test-NvmShadowFile -Path (Join-Path $sharedSave 'nvmshadow.bin')
        if (-not $nvm.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = $nvm.Reason } }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Cross-disc nvmshadow.bin preserved across Disc 1 -> Disc 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S05' -Tier 4 -Feature 'F3+F4-F11+F14' -Title 'Scenario 5: Complete CTest suite gate under MB_MUTE=1' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -TimeoutSec 600
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = "CTest failed in $BuildDir ($($chk.Detail))" }
        }
        $minTests = if ($BuildDir -eq 'build') { 59 } else { 61 }
        if ($chk.Count -lt $minTests) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Expected >= $minTests tests in $BuildDir, got $($chk.Count)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "100% pass ($($chk.Count)/$($chk.Count)) in $BuildDir under MB_MUTE=1" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S06' -Tier 4 -Feature 'F1+F2+F12+F13' -Title 'Scenario 6: Public release readiness audit (Root + .gitignore + UTF-8 Log + Git HEAD)' -Body {
        param($BuildDir)
        $loose = @(Get-ChildItem -LiteralPath $script:RepoRoot -File | Where-Object { $_.Extension -in @('.obj', '.exe') }).Count
        $tempDirs = @(Get-ChildItem -LiteralPath $script:RepoRoot -Directory | Where-Object { $_.Name -eq 'test_audio_reference' -or $_.Name -like 'test_save_*' -or $_.Name -in @('build-cpath-validation', 'build-msvc-utf8-check', 'build-mt-validation', 'dist-windows-clean') }).Count
        if ($loose -gt 0 -or $tempDirs -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Root not clean: loose=$loose tempOrObsolete=$tempDirs" }
        }
        $ig = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'disco1/', 'disco2/', 'test_audio_reference/', 'test_save_x/')
        if ($ig.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = '.gitignore directory shielding incomplete' }
        }
        $bytes = [System.IO.File]::ReadAllBytes($script:LogPath)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'LOG-DESENVOLVIMENTO.md has UTF-8 BOM' }
        }
        $exe = Get-BuildBinaryPath -BuildDir 'build-static' -BinaryName 'mrbones.exe'
        $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $logText = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8)
        if (-not $logText.ToLowerInvariant().Contains($hash)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "LOG-DESENVOLVIMENTO.md missing current SHA-256 $hash" }
        }
        $head = Invoke-GitReadOnly -ArgList @('rev-parse', 'HEAD')
        $base = Invoke-GitReadOnly -ArgList @('rev-parse', '370786a')
        if ($base.ExitCode -eq 0 -and $head.StdOut.Trim() -eq $base.StdOut.Trim()) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Local Git commit not yet created beyond 370786a' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Public release readiness audit passed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T4-S07' -Tier 4 -Feature 'F1+F3+F14' -Title 'Scenario 7: Silent audio & isolation compliance audit (CTest JSON + host_audio + movie_audio)' -Body {
        param($BuildDir)
        $meta = Get-CTestMetadata -BuildDir $BuildDir
        foreach ($name in @('test_mrbones_paths', 'test_movie_audio', 'test_full_campaign', 'test_fmv_transition')) {
            $entry = $meta.tests | Where-Object { $_.name -eq $name } | Select-Object -First 1
            if ($null -eq $entry) { return [PSCustomObject]@{ Passed = $false; Detail = "Missing CTest $name" } }
            $props = Get-CTestPropertyMap -TestEntry $entry
            $envList = if ($props.ContainsKey('ENVIRONMENT')) { @($props['ENVIRONMENT']) } else { @() }
            if (-not ($envList | Where-Object { $_ -match '^MB_MUTE=1$' })) {
                return [PSCustomObject]@{ Passed = $false; Detail = "CTest $name missing MB_MUTE=1" }
            }
        }
        $ha = Get-HostAudioAnalysis
        if (-not $ha.SupportsOn -or -not $ha.Pcm8Guarded -or $ha.NaiveO) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'host_audio.c missing pcm8_thread or safe "on"/"ON" guard' }
        }
        $r = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        if ($r.ExitCode -ne 0 -or $r.StdOut -match '(?m)^SKIP') {
            return [PSCustomObject]@{ Passed = $false; Detail = "test_movie_audio failed or skipped (exit $($r.ExitCode))" }
        }
        if ($r.CreatedAudioRefInRoot) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'test_audio_reference leaked into repo root' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Silent audio & CTest isolation 100% compliant' }
    }))

    return $tests.ToArray()
}
