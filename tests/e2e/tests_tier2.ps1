# Tier 2 Boundary, Corner & Negative Tests (70 tests: 5 per feature F1-F14)

function Get-Tier2Tests {
    $tests = New-Object System.Collections.Generic.List[object]

    # --- F1: Root directory cleanup boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F1-01' -Tier 2 -Feature 'F1' -Title 'All 36 legacy loose .obj and .exe filenames absent from root' -Body {
        param($BuildDir)
        $legacy = @(
            'clock-signal.obj', 'diag_decomp.obj', 'dump_actual_runway.obj', 'dump_mauso_frame.obj',
            'dump_menu_frame.obj', 'dump_runway_frame.obj', 'fade_probe.obj', 'finale-compare.obj',
            'finale-winref-compare.obj', 'fmv-trace-current.obj', 'guitar-compare.obj', 'host_audio.obj',
            'inspect_atil.obj', 'inspect_files.obj', 'inspect_mauso.obj', 'list_mauso_dir.obj',
            'locate-finale.obj', 'mute_probe.obj', 'probe-native-frame.obj', 'probe.obj',
            'scan_atil.obj', 'test-corrupt-disc.obj', 'test_atil_decomp.obj', 'test_banks.obj',
            'test_mauso_decomp.obj', 'test_runway_grid.obj', 'test_runway_ground.obj',
            'diag_decomp.exe', 'dump_mauso_frame.exe', 'inspect_atil.exe', 'inspect_files.exe',
            'list_mauso_dir.exe', 'scan_atil.exe', 'test_mauso_decomp.exe',
            'test_runway_grid.exe', 'test_runway_ground.exe'
        )
        $stillPresent = @($legacy | Where-Object { Test-Path -LiteralPath (Join-Path $script:RepoRoot $_) })
        if ($stillPresent.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "$($stillPresent.Count)/36 legacy root artifacts still present (e.g. $($stillPresent[0]))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0/36 legacy loose files present' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F1-02' -Tier 2 -Feature 'F1' -Title 'Codex build-chatgpt/ evidence & binaries preserved while obsolete dirs gone' -Body {
        param($BuildDir)
        $codexSub = @('build-chatgpt\debug', 'build-chatgpt\release-static')
        foreach ($sub in $codexSub) {
            if (-not (Test-Path -LiteralPath (Join-Path $script:RepoRoot $sub))) {
                return [PSCustomObject]@{ Passed = $false; Detail = "Codex dir missing: $sub" }
            }
        }
        if (Test-Path -LiteralPath (Join-Path $script:RepoRoot 'build-msvc-utf8-check')) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Obsolete build-msvc-utf8-check still exists' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Codex build-chatgpt preserved and obsolete validation dirs absent' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F1-03' -Tier 2 -Feature 'F1' -Title 'Root directory free of stray nvmshadow.bin' -Body {
        param($BuildDir)
        if (Test-Path -LiteralPath (Join-Path $script:RepoRoot 'nvmshadow.bin')) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Stray nvmshadow.bin found in repo root' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'No nvmshadow.bin in repo root' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F1-04' -Tier 2 -Feature 'F1' -Title 'Retail Disc 1 & Disc 2 Track 01 binary images intact (>250 MB)' -Body {
        param($BuildDir)
        $t1 = Join-Path $script:RepoRoot 'disco1\Mr. Bones (USA) (Disc 1) (Track 01).bin'
        $t2 = Join-Path $script:RepoRoot 'disco2\Mr. Bones (USA) (Disc 2) (Track 01).bin'
        if (-not (Test-Path -LiteralPath $t1) -or -not (Test-Path -LiteralPath $t2)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing Disc 1 or Disc 2 Track 01 .bin' }
        }
        $s1 = (Get-Item -LiteralPath $t1).Length
        $s2 = (Get-Item -LiteralPath $t2).Length
        if ($s1 -lt 250MB -or $s2 -lt 250MB) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Track 01 sizes too small: d1=$s1 d2=$s2" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "d1=$s1 B, d2=$s2 B" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F1-05' -Tier 2 -Feature 'F1' -Title 'Isolated mrbones run does not create or modify %LOCALAPPDATA%/MrBonesPort' -Body {
        param($BuildDir)
        $appSaves = Join-Path $env:LOCALAPPDATA 'MrBonesPort\saves\nvmshadow.bin'
        $existedBefore = Test-Path -LiteralPath $appSaves
        $mtimeBefore = if ($existedBefore) { (Get-Item -LiteralPath $appSaves).LastWriteTimeUtc } else { [DateTime]::MinValue }
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "mrbones failed (exit $($r.ExitCode))" }
        }
        $existedAfter = Test-Path -LiteralPath $appSaves
        $mtimeAfter = if ($existedAfter) { (Get-Item -LiteralPath $appSaves).LastWriteTimeUtc } else { [DateTime]::MinValue }
        if ($existedBefore -ne $existedAfter -or $mtimeBefore -ne $mtimeAfter) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Isolated run mutated %LOCALAPPDATA%/MrBonesPort/saves/nvmshadow.bin' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '%LOCALAPPDATA%/MrBonesPort untouched' }
    }))

    # --- F2: .gitignore shielding boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F2-01' -Tier 2 -Feature 'F2' -Title 'git check-ignore matches arbitrary test_save_*/ pattern' -Body {
        param($BuildDir)
        $r = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'test_save_e2e_probe/')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "test_save_e2e_probe/ not ignored (exit $($r.ExitCode))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'test_save_*/ glob ignores arbitrary test_save dirs' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F2-02' -Tier 2 -Feature 'F2' -Title 'git check-ignore ignores binary disc tracks inside disco1/ and disco2/' -Body {
        param($BuildDir)
        $r1 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'disco1/Mr. Bones (USA) (Disc 1) (Track 01).bin')
        $r2 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'disco2/Mr. Bones (USA) (Disc 2) (Track 01).bin')
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Disc track binaries not ignored (d1=$($r1.ExitCode), d2=$($r2.ExitCode))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Disc tracks ignored' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F2-03' -Tier 2 -Feature 'F2' -Title 'git check-ignore ignores build/, build-static/, and build-chatgpt/' -Body {
        param($BuildDir)
        foreach ($d in @('build/', 'build-static/', 'build-chatgpt/')) {
            $r = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', $d)
            if ($r.ExitCode -ne 0) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$d not ignored (exit $($r.ExitCode))" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All build directories ignored' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F2-04' -Tier 2 -Feature 'F2' -Title 'git check-ignore does NOT ignore assets/sprites_bones/README.txt or PNGs' -Body {
        param($BuildDir)
        $r = Invoke-GitReadOnly -ArgList @('check-ignore', 'assets/sprites_bones/README.txt', 'assets/sprites_bones/hd_3x')
        if ($r.ExitCode -ne 1 -or -not [string]::IsNullOrWhiteSpace($r.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'assets/sprites_bones/README.txt and hd_3x unignored' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F2-05' -Tier 2 -Feature 'F2' -Title 'git check-ignore does NOT ignore core source, CMakeLists.txt, or log' -Body {
        param($BuildDir)
        $r = Invoke-GitReadOnly -ArgList @('check-ignore', 'LOG-DESENVOLVIMENTO.md', 'CMakeLists.txt', 'src/tools/mrbones.c')
        if ($r.ExitCode -ne 1 -or -not [string]::IsNullOrWhiteSpace($r.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Core files unignored' }
    }))

    # --- F3: Inaudible audio & CTest boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F3-01' -Tier 2 -Feature 'F3' -Title 'host_audio.c is_muted() rejects naive "o" prefix so "off" and "0" are not muted' -Body {
        param($BuildDir)
        $ha = Get-HostAudioAnalysis
        if (-not $ha.Exists) { return [PSCustomObject]@{ Passed = $false; Detail = $ha.Reason } }
        if ($ha.NaiveO) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'is_muted() naively matches env[0]==''o'', which falsely mutes "off"' }
        }
        if (-not $ha.SupportsOn) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'is_muted() does not yet support "on"/"ON" safely' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'is_muted() cleanly distinguishes "on"/"ON" from "off"/"0"' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F3-02' -Tier 2 -Feature 'F3' -Title 'test_movie_audio.exe executes virtual clock without SKIP lines' -Body {
        param($BuildDir)
        $res = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        $skips = @($res.StdOut -split "`r?`n" | Where-Object { $_ -match '^SKIP' })
        if ($res.ExitCode -ne 0 -or $skips.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($res.ExitCode) skips=$($skips.Count)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 SKIP lines, virtual clock completed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F3-03' -Tier 2 -Feature 'F3' -Title 'test_movie_audio.exe isolates test_audio_reference inside CWD, not root' -Body {
        param($BuildDir)
        $res = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        if ($res.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($res.ExitCode)" } }
        if (-not $res.CreatedAudioRefInWd -or $res.CreatedAudioRefInRoot) {
            return [PSCustomObject]@{ Passed = $false; Detail = "inWd=$($res.CreatedAudioRefInWd) inRoot=$($res.CreatedAudioRefInRoot)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'test_audio_reference contained inside temp CWD' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F3-04' -Tier 2 -Feature 'F3' -Title 'Finale replay CTest targets pass silently with MB_MUTE=1' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^test_finale_(saturn_replay|short_pulse|displaced_pulse)$' -TimeoutSec 120
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '3 finale replay CTest targets passed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F3-05' -Tier 2 -Feature 'F3' -Title 'test_duck_no_ffmpeg passes without external ffmpeg/audio dependency' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^test_duck_no_ffmpeg$' -TimeoutSec 60
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'test_duck_no_ffmpeg passed' }
    }))

    # --- F4: 06VALLEY boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F4-01' -Tier 2 -Feature 'F4' -Title 'Negative: 06VALLEY CFence blocks advance before paired CFenceKey' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VALLEY:.*VALLEY finished \(EndLevel win\)' `
            -RequiredSourceRegex '(?s)(CFence|fence).*?(block|blocked|!.*finished|STATE.*!=.*FINISHED)' `
            -StageLabel '06VALLEY negative fence'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F4-02' -Tier 2 -Feature 'F4' -Title 'Boundary: 06VALLEY wrong CFenceKey ID does not unlock unpaired CFence' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VALLEY:.*VALLEY finished \(EndLevel win\)' `
            -RequiredSourceRegex '(?s)(fence_key|CFenceKey).*?(id|0x13000000|unpaired|wrong)' `
            -StageLabel '06VALLEY wrong key ID'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F4-03' -Tier 2 -Feature 'F4' -Title 'Boundary: 06VALLEY CWinBox 2D bounds (68, 10000x10000) verified' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VALLEY:.*amap=494.*VALLEY finished \(EndLevel win\)' `
            -RequiredSourceRegex 'finish_y == 68 && phase\.finish_width == 10000 && phase\.finish_height == 10000' `
            -StageLabel '06VALLEY CWinBox bounds'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F4-04' -Tier 2 -Feature 'F4' -Title 'Negative: --level 06VALLEY without leading slash exits 1' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '06VALLEY', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 1) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Expected exit 1, got $($r.ExitCode)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected missing slash with exit 1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F4-05' -Tier 2 -Feature 'F4' -Title 'Negative: missing Disc 1 cue file exits 1 with disc: error' -Body {
        param($BuildDir)
        $missingCue = Join-Path (New-E2ETempSubdir -Prefix 'nocue') 'missing.cue'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($missingCue, $script:ManifestPath, '--level', '/06VALLEY', '--autoplay')
        if ($r.ExitCode -ne 1 -or $r.StdErr -notmatch 'disc:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing disc1 rejected with exit 1' }
    }))

    # --- F5: 08BAT1 boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F5-01' -Tier 2 -Feature 'F5' -Title 'Negative: 08BAT1 wrong body_config at exit does not finish' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'BAT1:.*BAT1 finished \(EndLevel win\)' `
            -RequiredSourceRegex 'verify_batwood\s*\([^;]*"/08BAT1"' `
            -StageLabel '08BAT1 exit config negative'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F5-02' -Tier 2 -Feature 'F5' -Title 'Boundary: 08BAT1 off-Y at exit fails batwood_exit_contact' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'BAT1:.*BAT1 finished \(EndLevel win\)' `
            -RequiredSourceRegex 'verify_batwood\s*\([^;]*"/08BAT1".*?!batwood_exit_contact' `
            -StageLabel '08BAT1 exit Y boundary'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F5-03' -Tier 2 -Feature 'F5' -Title 'Boundary: --benchmark-frames 0 rejected with exit 2 and usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--benchmark-frames', '0')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected --benchmark-frames 0 with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F5-04' -Tier 2 -Feature 'F5' -Title 'Boundary: --benchmark-frames 1000001 rejected with exit 2 and usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--benchmark-frames', '1000001')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected --benchmark-frames 1000001 with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F5-05' -Tier 2 -Feature 'F5' -Title 'Negative: non-numeric --benchmark-frames abc rejected with exit 2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--benchmark-frames', 'abc')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected non-numeric benchmark-frames with exit 2' }
    }))

    # --- F6: 05MAUSO boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F6-01' -Tier 2 -Feature 'F6' -Title 'Negative: 05MAUSO incomplete CMaestro waves block stage completion' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'MAUSO:.*MAUSO finished \(EndLevel win\)' `
            -RequiredSourceRegex '(?s)(CMaestro|maestro).*?(SIDESCRL_ST_FINISHED|wave|CButFly)' `
            -StageLabel '05MAUSO CMaestro negative'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F6-02' -Tier 2 -Feature 'F6' -Title 'Boundary: 05MAUSO Down tall toggle, Blues fire, and left edge clamp' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'MAUSO:.*MAUSO finished \(EndLevel win\)' `
            -RequiredSourceRegex 'test_world_edges' `
            -StageLabel '05MAUSO edges/tall'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F6-03' -Tier 2 -Feature 'F6' -Title 'Negative: missing manifest file exits 1 with manifest: on stderr' -Body {
        param($BuildDir)
        $missingMan = Join-Path (New-E2ETempSubdir -Prefix 'noman') 'missing_versions.txt'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $missingMan, '--level', '/05MAUSO', '--autoplay')
        if ($r.ExitCode -ne 1 -or $r.StdErr -notmatch 'manifest:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing manifest rejected with exit 1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F6-04' -Tier 2 -Feature 'F6' -Title 'Boundary: --benchmark-frames 1 minimum succeeds with exactly 1 frame_time row' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--benchmark-frames', '1', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 1
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F6-05' -Tier 2 -Feature 'F6' -Title 'Negative: argc == 2 (missing manifest arg) exits 2 with usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue) -OmitSaveDir -OmitMuteFlag
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing manifest positional arg rejected with exit 2' }
    }))

    # --- F7: 19VILLAGE boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F7-01' -Tier 2 -Feature 'F7' -Title 'Negative: /19VILLAGE without --disc2 fails cleanly with begin failed and exit 1' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/19VILLAGE', '--autoplay')
        if ($r.ExitCode -ne 1 -or ($r.StdOut + $r.StdErr) -notmatch 'full: begin failed /19VILLAGE|INSERTCD: need disc 2 for /19VILLAGE') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing --disc2 rejected with exit 1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F7-02' -Tier 2 -Feature 'F7' -Title 'Negative: 19VILLAGE EatLittleGuy does not increment _LittleGuysSaved' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VILLAGE:.*VILLAGE finished \(EndLevel win\)' `
            -RequiredSourceRegex '(EatLittleGuy|eat_little_guy).*?(saved|LittleGuysSaved|CMeterSpider)' `
            -StageLabel '19VILLAGE EatLittleGuy negative'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F7-03' -Tier 2 -Feature 'F7' -Title 'Negative: passing Disc 1 cue as --disc2 fails identity disc2 check' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc1Cue, '--level', '/19VILLAGE', '--autoplay')
        if ($r.ExitCode -ne 1 -or $r.StdErr -notmatch 'identity disc2:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Wrong Disc 2 identity rejected with exit 1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F7-04' -Tier 2 -Feature 'F7' -Title 'Boundary: mbverify with valid disc1 and missing disc2 exits 3' -Body {
        param($BuildDir)
        $missingD2 = Join-Path (New-E2ETempSubdir -Prefix 'nod2') 'missing_disc2.cue'
        $r = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--disc1', $script:Disc1Cue, '--disc2', $missingD2, '--manifest', $script:ManifestPath, '--quick')
        if ($r.ExitCode -ne 3 -or $r.StdOut -notmatch 'disc 1: OK' -or ($r.StdOut + $r.StdErr) -notmatch 'disc 2: FAIL') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'mbverify reported disc 1 OK, disc 2 FAIL, exit 3' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F7-05' -Tier 2 -Feature 'F7' -Title 'Boundary: --level VILLAGE (stage name without /19 prefix) resolves and wins' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', 'VILLAGE', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /19VILLAGE disc=2') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Name lookup VILLAGE resolved /19VILLAGE' }
    }))

    # --- F8: 26LAFF boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F8-01' -Tier 2 -Feature 'F8' -Title 'Negative: 26LAFF exit lock blocks finish_x while boss/meter active' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'LAFF:.*boss_rem=0 LAFF finished \(EndLevel win\)' `
            -RequiredSourceRegex 'phase\.state != SIDESCRL_ST_FINISHED && phase\.bones_x < phase\.finish_x' `
            -StageLabel '26LAFF exit lock'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F8-02' -Tier 2 -Feature 'F8' -Title 'Negative: 26LAFF CJunior::ToldGoodJoke triggers CringeBrain without Laff advance' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'LAFF:.*LAFF finished \(EndLevel win\)' `
            -RequiredSourceRegex '(ToldGoodJoke|good_joke).*?(CringeBrain|cringe)' `
            -StageLabel '26LAFF ToldGoodJoke negative'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F8-03' -Tier 2 -Feature 'F8' -Title 'Negative: /26LAFF without --disc2 fails cleanly with begin failed and exit 1' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/26LAFF', '--autoplay')
        if ($r.ExitCode -ne 1 -or ($r.StdOut + $r.StdErr) -notmatch 'full: begin failed /26LAFF|INSERTCD: need disc 2 for /26LAFF') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing --disc2 rejected for /26LAFF' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F8-04' -Tier 2 -Feature 'F8' -Title 'Boundary: --scale 5 rejected with exit 2 and usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/26LAFF', '--scale', '5')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected --scale 5 with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F8-05' -Tier 2 -Feature 'F8' -Title 'Negative: --upscale bogus rejected with exit 2 and usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/26LAFF', '--upscale', 'bogus')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected --upscale bogus with exit 2' }
    }))

    # --- F9: 15GLASS boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F9-01' -Tier 2 -Feature 'F9' -Title 'Negative: 15GLASS unopened CKeyHole locks exit' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'GLASS:.*GLASS finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CKeyHole|keyhole).*?(locked|!.*FINISHED|!= SIDESCRL_ST_FINISHED)' `
            -StageLabel '15GLASS CKeyHole lock'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F9-02' -Tier 2 -Feature 'F9' -Title 'Negative: 15GLASS CTrapSha / cracking crystal collapses and drops actor' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'GLASS:.*GLASS finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CTrapSha|trap_sha|crack|shatter).*?(GLASS|15GLASS)' `
            -StageLabel '15GLASS CTrapSha'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F9-03' -Tier 2 -Feature 'F9' -Title 'Boundary: --scale 0 rejected with exit 2 and usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--scale', '0')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Rejected --scale 0 with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F9-04' -Tier 2 -Feature 'F9' -Title 'Negative: --finale-replay combined with --level /15GLASS exits 2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--finale-replay', 'dummy.csv', '--level', '/15GLASS')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Conflicting --finale-replay + --level rejected with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F9-05' -Tier 2 -Feature 'F9' -Title 'Negative: unknown flag --badflag rejected with exit 2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--badflag')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Unknown flag rejected with exit 2' }
    }))

    # --- F10: 17VORTEX boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F10-01' -Tier 2 -Feature 'F10' -Title 'Negative: 17VORTEX incomplete CGlass waves block _TryEndingVortexLevel' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VORTEX:.*VORTEX finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CGlass|TryEndingVortex|vortex_wave).*?(!= SIDESCRL_ST_FINISHED|blocked|incomplete)' `
            -StageLabel '17VORTEX negative wave'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F10-02' -Tier 2 -Feature 'F10' -Title 'Boundary: 17VORTEX free-flight vertical clamping and POOF_BLUE checks' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VORTEX:.*VORTEX finished \(EndLevel win\)' `
            -RequiredSourceRegex 'STAGE_ENEMY_POOF_BLUE' `
            -StageLabel '17VORTEX POOF_BLUE'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F10-03' -Tier 2 -Feature 'F10' -Title 'Negative: invalid stage --level /17VORTEXX exits 1' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/17VORTEXX', '--autoplay')
        if ($r.ExitCode -ne 1) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Expected exit 1, got $($r.ExitCode)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Invalid stage /17VORTEXX exited 1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F10-04' -Tier 2 -Feature 'F10' -Title 'Boundary: EMPTY stage --level /12TROLL1 skips forward to /14DRUM' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/12TROLL1', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /14DRUM') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '/12TROLL1 skipped cleanly to /14DRUM' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F10-05' -Tier 2 -Feature 'F10' -Title 'Boundary: 17VORTEX benchmark with --scale 2 --upscale xbr2x succeeds' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/17VORTEX', '--benchmark-frames', '10', '--scale', '2', '--upscale', 'xbr2x')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'upscale: xbr2x 2x \(640x448\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 10
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    # --- F11: 22SHADOW boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F11-01' -Tier 2 -Feature 'F11' -Title 'Negative: 22SHADOW trials <= 6 blocks _IsEndOfLevel__7CSpirit' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'SHADOW:.*SHADOW finished \(EndLevel win\)' `
            -RequiredSourceRegex '(?s)(trials|CSpirit).*?(<= 6|< 7|!= SIDESCRL_ST_FINISHED)' `
            -StageLabel '22SHADOW trials<=6'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F11-02' -Tier 2 -Feature 'F11' -Title 'Negative: 22SHADOW wrong config (!= _SSRHAALL_config) blocks end of level' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'SHADOW:.*SHADOW finished \(EndLevel win\)' `
            -RequiredSourceRegex '(SSRHAALL|shadow_config).*?(!=|wrong|!= SIDESCRL_ST_FINISHED)' `
            -StageLabel '22SHADOW wrong config'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F11-03' -Tier 2 -Feature 'F11' -Title 'Negative: 22SHADOW crimson gaze drains uncrouched movement vs crouch safety' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'SHADOW:.*SHADOW finished \(EndLevel win\)' `
            -RequiredSourceRegex '(eye_phase|gaze|crouch).*?(22SHADOW|SHADOW)' `
            -StageLabel '22SHADOW gaze stealth'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F11-04' -Tier 2 -Feature 'F11' -Title 'Negative: /22SHADOW without --disc2 fails cleanly with begin failed and exit 1' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/22SHADOW', '--autoplay')
        if ($r.ExitCode -ne 1 -or ($r.StdOut + $r.StdErr) -notmatch 'full: begin failed /22SHADOW|INSERTCD: need disc 2 for /22SHADOW') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing --disc2 rejected for /22SHADOW' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F11-05' -Tier 2 -Feature 'F11' -Title 'Negative: missing argument after --scale exits 2 with usage' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/22SHADOW', '--scale') -OmitSaveDir -OmitMuteFlag
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Missing --scale value rejected with exit 2' }
    }))

    # --- F12: LOG-DESENVOLVIMENTO.md boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F12-01' -Tier 2 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md has no UTF-8 or UTF-16 BOM' -Body {
        param($BuildDir)
        $b = [System.IO.File]::ReadAllBytes($script:LogPath)
        if ($b.Length -lt 4) { return [PSCustomObject]@{ Passed = $false; Detail = 'Log too short' } }
        $hasUtf8Bom = ($b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)
        $hasUtf16Bom = (($b[0] -eq 0xFF -and $b[1] -eq 0xFE) -or ($b[0] -eq 0xFE -and $b[1] -eq 0xFF))
        if ($hasUtf8Bom -or $hasUtf16Bom) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Detected BOM in LOG-DESENVOLVIMENTO.md' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'No BOM bytes' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F12-02' -Tier 2 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md contains zero NUL bytes or U+FFFD' -Body {
        param($BuildDir)
        $b = [System.IO.File]::ReadAllBytes($script:LogPath)
        if ($b -contains 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'NUL byte detected in LOG-DESENVOLVIMENTO.md' }
        }
        $t = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8)
        if ($t.Contains([char]0xFFFD)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Replacement char U+FFFD detected' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 NUL bytes, 0 U+FFFD' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F12-03' -Tier 2 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md latest entry matches current build-static SHA-256 and DLL list' -Body {
        param($BuildDir)
        $exe = Get-BuildBinaryPath -BuildDir 'build-static' -BinaryName 'mrbones.exe'
        $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines = [System.IO.File]::ReadAllLines($script:LogPath, [System.Text.Encoding]::UTF8)
        if ($lines.Count -le 1650) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Log has no new entry beyond line 1648' }
        }
        $tail = ($lines[1648..($lines.Count - 1)]) -join "`n"
        if ($tail.ToLowerInvariant() -notmatch $hash -or $tail -notmatch 'KERNEL32|kernel32') {
            return [PSCustomObject]@{ Passed = $false; Detail = "Tail entry missing live SHA-256 ($hash) or DLL verification" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Tail entry matches live binary hash & DLLs' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F12-04' -Tier 2 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md preserves original H1 title and >= 326 KB history' -Body {
        param($BuildDir)
        $len = (Get-Item -LiteralPath $script:LogPath).Length
        $first = (Get-Content -LiteralPath $script:LogPath -TotalCount 1 -Encoding UTF8)
        if ($len -lt 326000 -or $first -notmatch '^# Log de desenvolvimento') {
            return [PSCustomObject]@{ Passed = $false; Detail = "len=$len first='$first'" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Preserved history ($len bytes)" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F12-05' -Tier 2 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md new section uses dated/titled H2 heading without empty headings' -Body {
        param($BuildDir)
        $lines = [System.IO.File]::ReadAllLines($script:LogPath, [System.Text.Encoding]::UTF8)
        $emptyHeadings = @($lines | Where-Object { $_ -match '^#{1,6}\s*$' })
        if ($emptyHeadings.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Found empty Markdown heading' }
        }
        if ($lines.Count -le 1650) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'No new dated H2 section added beyond line 1648' }
        }
        $tailHeadings = @($lines[1648..($lines.Count - 1)] | Where-Object { $_ -match '^##\s+\S+' })
        if ($tailHeadings.Count -lt 1) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'New log entries missing ## H2 section heading' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Found $($tailHeadings.Count) new H2 heading(s)" }
    }))

    # --- F13: Git local commit boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F13-01' -Tier 2 -Feature 'F13' -Title 'HEAD tracks zero .bin or .cue retail disc files' -Body {
        param($BuildDir)
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        $bad = @($tree.StdOut -split "`r?`n" | Where-Object { $_ -match '\.(bin|cue)$' -and $_ -notmatch 'tests/' })
        if ($bad.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Tracked .bin/.cue in HEAD: $($bad[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 disc .bin/.cue files tracked' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F13-02' -Tier 2 -Feature 'F13' -Title 'HEAD tracks zero .obj, .exe, .dll, .lib, or .pdb binaries' -Body {
        param($BuildDir)
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        $bad = @($tree.StdOut -split "`r?`n" | Where-Object { $_ -match '\.(obj|exe|dll|lib|pdb)$' })
        if ($bad.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Tracked binary in HEAD: $($bad[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 compiled binaries tracked in HEAD' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F13-03' -Tier 2 -Feature 'F13' -Title 'HEAD tracks zero test_save_* or test_audio_reference paths' -Body {
        param($BuildDir)
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        $bad = @($tree.StdOut -split "`r?`n" | Where-Object { $_ -match '(^|/)(test_save_|test_audio_reference)' })
        if ($bad.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Tracked temp save dir in HEAD: $($bad[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 temp save dirs tracked in HEAD' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F13-04' -Tier 2 -Feature 'F13' -Title 'Local git repository integrity check (rev-parse HEAD + branch) passes' -Body {
        param($BuildDir)
        $r = Invoke-GitReadOnly -ArgList @('rev-parse', '--verify', 'HEAD')
        $b = Invoke-GitReadOnly -ArgList @('branch', '--show-current')
        if ($r.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($b.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Git HEAD/branch check failed" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Branch=$($b.StdOut.Trim())" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F13-05' -Tier 2 -Feature 'F13' -Title 'Git config/remotes contain zero embedded credentials or tokens' -Body {
        param($BuildDir)
        $rem = Invoke-GitReadOnly -ArgList @('remote', '-v')
        if ($rem.StdOut -match '://[^/\s]+@|ghp_|github_pat_') {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Embedded credential/token found in git remote URL' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'No credentials in git remotes' }
    }))

    # --- F14: Final E2E & Benchmarks boundary/negative (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F14-01' -Tier 2 -Feature 'F14' -Title 'Negative: mbverify.exe --badflag exits 3 with usage' -Body {
        param($BuildDir)
        $r = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--badflag')
        if ($r.ExitCode -ne 3 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'mbverify --badflag exited 3' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F14-02' -Tier 2 -Feature 'F14' -Title 'Negative: mbverify.exe missing manifest exits 3' -Body {
        param($BuildDir)
        $missing = Join-Path (New-E2ETempSubdir -Prefix 'mbv_noman') 'missing.txt'
        $r = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--disc1', $script:Disc1Cue, '--manifest', $missing)
        if ($r.ExitCode -ne 3 -or ($r.StdOut + $r.StdErr) -notmatch 'cannot open manifest:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'mbverify missing manifest exited 3' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F14-03' -Tier 2 -Feature 'F14' -Title 'Negative: mbverify.exe with empty config and no discs exits 3' -Body {
        param($BuildDir)
        $r = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--manifest', $script:ManifestPath)
        if ($r.ExitCode -ne 3 -or ($r.StdOut + $r.StdErr) -notmatch 'no disc configured') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'mbverify no disc configured exited 3' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F14-04' -Tier 2 -Feature 'F14' -Title 'Negative: --finale-replay combined with --autoplay exits 2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--finale-replay', 'dummy.csv', '--autoplay')
        if ($r.ExitCode -ne 2 -or $r.StdErr -notmatch 'usage:') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Mutual exclusion rejected with exit 2' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T2-F14-05' -Tier 2 -Feature 'F14' -Title 'Boundary: MB_CONFIG pointing to temp INI manifest overrides defaults (exit 1)' -Body {
        param($BuildDir)
        $tmpDir = New-E2ETempSubdir -Prefix 'mbcfg'
        $iniPath = Join-Path $tmpDir 'override.ini'
        $badMan = Join-Path $tmpDir 'missing-ini-manifest.txt'
        [System.IO.File]::WriteAllText($iniPath, "manifest=$badMan`n", [System.Text.UTF8Encoding]::new($false))
        $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'mrbones.exe'
        $r = Invoke-IsolatedProcess -FilePath $exe -WorkingDirectory $tmpDir -EnvOverrides @{
            MB_MUTE   = '1'
            MB_CONFIG = $iniPath
        } -TimeoutSec 15
        if ($r.ExitCode -ne 1 -or $r.StdErr -notmatch 'missing-ini-manifest') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'MB_CONFIG precedence verified cleanly' }
    }))

    return $tests.ToArray()
}
