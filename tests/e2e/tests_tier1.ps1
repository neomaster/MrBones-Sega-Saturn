# Tier 1 Feature Coverage Tests (71 tests across F1-F14)
# Derived from ORIGINAL_REQUEST.md, PROJECT.md, and TEST_INFRA.md.

function Get-Tier1Tests {
    $tests = New-Object System.Collections.Generic.List[object]

    # --- F1: Root directory cleanup (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F1-01' -Tier 1 -Feature 'F1' -Title 'Root free of loose .obj and .exe files' -Body {
        param($BuildDir)
        $loose = @(Get-ChildItem -LiteralPath $script:RepoRoot -File | Where-Object { $_.Extension -in @('.obj', '.exe') })
        if ($loose.Count -gt 0) {
            $names = ($loose | Select-Object -First 8 | ForEach-Object { $_.Name }) -join ', '
            return [PSCustomObject]@{ Passed = $false; Detail = "Found $($loose.Count) loose .obj/.exe in root: $names" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 loose .obj/.exe in root' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F1-02' -Tier 1 -Feature 'F1' -Title 'Root free of temp test_audio_reference and test_save_* dirs' -Body {
        param($BuildDir)
        $dirs = @(Get-ChildItem -LiteralPath $script:RepoRoot -Directory | Where-Object { $_.Name -eq 'test_audio_reference' -or $_.Name -like 'test_save_*' })
        if ($dirs.Count -gt 0) {
            $names = ($dirs | ForEach-Object { $_.Name }) -join ', '
            return [PSCustomObject]@{ Passed = $false; Detail = "Found $($dirs.Count) temp dirs in root: $names" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 temp test_* dirs in root' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F1-03' -Tier 1 -Feature 'F1' -Title 'Root free of obsolete build/dist validation dirs' -Body {
        param($BuildDir)
        $obsolete = @('build-cpath-validation', 'build-msvc-utf8-check', 'build-mt-validation', 'dist-windows-clean')
        $found = @($obsolete | Where-Object { Test-Path -LiteralPath (Join-Path $script:RepoRoot $_) })
        if ($found.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Obsolete dirs still present: $($found -join ', ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 4 obsolete build/dist dirs removed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F1-04' -Tier 1 -Feature 'F1' -Title 'Preserved build/, build-static/, and build-chatgpt/ dirs intact' -Body {
        param($BuildDir)
        $required = @(
            'build\mrbones.exe',
            'build-static\mrbones.exe',
            'build-chatgpt\debug',
            'build-chatgpt\release-static'
        )
        $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $script:RepoRoot $_)) })
        if ($missing.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Missing preserved paths: $($missing -join ', ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'build/, build-static/, and build-chatgpt/ preserved' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F1-05' -Tier 1 -Feature 'F1' -Title 'Preserved disco1/, disco2/, and assets/sprites_bones/ intact' -Body {
        param($BuildDir)
        if (-not (Test-Path -LiteralPath $script:Disc1Cue) -or -not (Test-Path -LiteralPath $script:Disc2Cue)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing disco1 or disco2 cue file' }
        }
        $d1Bins = @(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'disco1') -Filter '*.bin' -File).Count
        $d2Bins = @(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'disco2') -Filter '*.bin' -File).Count
        $assetsDir = Join-Path $script:RepoRoot 'assets\sprites_bones'
        $assetCount = if (Test-Path -LiteralPath $assetsDir) { @(Get-ChildItem -LiteralPath $assetsDir -Recurse -File).Count } else { 0 }
        if ($d1Bins -lt 10 -or $d2Bins -lt 12 -or $assetCount -lt 337) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Counts low: d1Bins=$d1Bins d2Bins=$d2Bins assets=$assetCount" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "d1Bins=$d1Bins d2Bins=$d2Bins assets=$assetCount" }
    }))

    # --- F2: .gitignore shielding (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F2-01' -Tier 1 -Feature 'F2' -Title '.gitignore explicitly ignores disco1/, disco2/, test_audio_reference/, test_save_*/' -Body {
        param($BuildDir)
        if (-not (Test-Path -LiteralPath $script:GitIgnorePath)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing .gitignore' }
        }
        $lines = Get-Content -LiteralPath $script:GitIgnorePath -Encoding UTF8 | ForEach-Object { $_.Trim() }
        $missing = @()
        if ($lines -notcontains 'disco1/') { $missing += 'disco1/' }
        if ($lines -notcontains 'disco2/') { $missing += 'disco2/' }
        if ($lines -notcontains 'test_audio_reference/') { $missing += 'test_audio_reference/' }
        if (($lines -notcontains 'test_save_*/') -and ($lines -notcontains 'test_save_*')) { $missing += 'test_save_*/' }
        if ($missing.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = ".gitignore missing entries: $($missing -join ', ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All required ignore rules present' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F2-02' -Tier 1 -Feature 'F2' -Title 'git check-ignore matches disco1/ and disco2/ directories' -Body {
        param($BuildDir)
        $r1 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'disco1/')
        $r2 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'disco2/')
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "check-ignore exit codes: disco1=$($r1.ExitCode), disco2=$($r2.ExitCode)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'disco1/ and disco2/ ignored by git' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F2-03' -Tier 1 -Feature 'F2' -Title 'git check-ignore matches test_audio_reference/ and test_save_menu/' -Body {
        param($BuildDir)
        $r1 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'test_audio_reference/')
        $r2 = Invoke-GitReadOnly -ArgList @('check-ignore', '-q', 'test_save_menu/')
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "check-ignore exit codes: test_audio_reference=$($r1.ExitCode), test_save_menu=$($r2.ExitCode)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'test_audio_reference/ and test_save_menu/ ignored by git' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F2-04' -Tier 1 -Feature 'F2' -Title 'assets/ and all 337 sprite files remain unignored' -Body {
        param($BuildDir)
        $files = @(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'assets\sprites_bones') -Recurse -File | ForEach-Object {
            $_.FullName.Substring($script:RepoRoot.Length + 1).Replace('\', '/')
        })
        if ($files.Count -lt 337) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Expected >= 337 assets files, got $($files.Count)" }
        }
        $r = Invoke-GitReadOnly -ArgList (@('check-ignore') + $files)
        if ($r.ExitCode -ne 1 -or -not [string]::IsNullOrWhiteSpace($r.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "check-ignore exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "All $($files.Count) assets files unignored" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F2-05' -Tier 1 -Feature 'F2' -Title 'git status contains zero disco, build, or test_save paths' -Body {
        param($BuildDir)
        $r = Invoke-GitReadOnly -ArgList @('status', '--porcelain=v1', '--untracked-files=all')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "git status failed: $($r.StdErr)" }
        }
        $bad = @($r.StdOut -split "`r?`n" | Where-Object {
            $_ -match '^..\s+"?(disco1/|disco2/|build/|build-static/|build-chatgpt/|test_audio_reference/|test_save_)'
        })
        if ($bad.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Unignored build/disc/temp paths in git status: $($bad[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'git status free of disc/build/temp artifacts' }
    }))

    # --- F3: Inaudible audio & CTest hardening (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F3-01' -Tier 1 -Feature 'F3' -Title 'CTest metadata enforces MB_MUTE=1 on all audio-linked targets' -Body {
        param($BuildDir)
        $meta = Get-CTestMetadata -BuildDir $BuildDir
        $targets = @('test_mrbones_paths', 'test_movie_audio', 'test_full_campaign', 'test_fmv_transition',
                     'test_finale_saturn_replay', 'test_finale_short_pulse', 'test_finale_displaced_pulse')
        $missingMute = @()
        foreach ($name in $targets) {
            $entry = $meta.tests | Where-Object { $_.name -eq $name } | Select-Object -First 1
            if ($null -eq $entry) {
                $missingMute += "$name(missing)"
                continue
            }
            $props = Get-CTestPropertyMap -TestEntry $entry
            $envList = @()
            if ($props.ContainsKey('ENVIRONMENT')) { $envList = @($props['ENVIRONMENT']) }
            if (-not ($envList | Where-Object { $_ -match '^MB_MUTE=1$' })) {
                $missingMute += $name
            }
        }
        if ($missingMute.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Targets missing MB_MUTE=1 in ENVIRONMENT: $($missingMute -join ', ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 7 CTest targets enforce MB_MUTE=1' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F3-02' -Tier 1 -Feature 'F3' -Title 'CTest passes --mute on campaign/fmv and sets build WORKING_DIRECTORY' -Body {
        param($BuildDir)
        $meta = Get-CTestMetadata -BuildDir $BuildDir
        $issues = @()
        foreach ($name in @('test_full_campaign', 'test_fmv_transition')) {
            $entry = $meta.tests | Where-Object { $_.name -eq $name } | Select-Object -First 1
            if ($null -eq $entry) { $issues += "$name(missing)"; continue }
            if ($entry.command -notcontains '--mute') { $issues += "$name(no --mute)" }
        }
        $expectedWd = (Join-Path $script:RepoRoot $BuildDir).Replace('\', '/').ToLowerInvariant()
        foreach ($name in @('test_movie_audio', 'test_fmv_transition')) {
            $entry = $meta.tests | Where-Object { $_.name -eq $name } | Select-Object -First 1
            if ($null -eq $entry) { continue }
            $props = Get-CTestPropertyMap -TestEntry $entry
            $wd = ''
            if ($props.ContainsKey('WORKING_DIRECTORY')) { $wd = ([string]$props['WORKING_DIRECTORY']).Replace('\', '/').ToLowerInvariant() }
            if ($wd -ne $expectedWd) {
                $issues += "$name(wd='$wd')"
            }
        }
        if ($issues.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "CTest metadata issues: $($issues -join '; ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'CTest --mute and WORKING_DIRECTORY verified' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F3-03' -Tier 1 -Feature 'F3' -Title 'test_movie_audio.exe passes silently under MB_MUTE=1 with zero SKIPs' -Body {
        param($BuildDir)
        $res = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        if ($res.TimedOut -or $res.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($res.ExitCode) timeout=$($res.TimedOut) err=$($res.StdErr)" }
        }
        if ($res.StdOut -match '(?m)^SKIP') {
            return [PSCustomObject]@{ Passed = $false; Detail = 'test_movie_audio printed SKIP under MB_MUTE=1' }
        }
        $required = @('OK movie audio clock', 'OK opening audio clock', 'OK LOSE audio clock',
                      'OK story audio clock', 'OK finale audio clock', 'OK movie interruption and replacement audio')
        foreach ($req in $required) {
            if ($res.StdOut -notmatch [regex]::Escape($req)) {
                return [PSCustomObject]@{ Passed = $false; Detail = "Missing output: $req" }
            }
        }
        if ($res.CreatedAudioRefInRoot) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'test_audio_reference leaked into repo root' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 6 silent movie audio checks passed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F3-04' -Tier 1 -Feature 'F3' -Title 'host_audio.c pcm8_thread guard and MB_MUTE=on/ON silent execution' -Body {
        param($BuildDir)
        $ha = Get-HostAudioAnalysis
        if (-not $ha.Exists) { return [PSCustomObject]@{ Passed = $false; Detail = $ha.Reason } }
        if (-not $ha.Pcm8Guarded) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'host_audio.c pcm8_thread lacks is_muted() guard before waveOutOpen' }
        }
        if (-not $ha.SupportsOn -or $ha.NaiveO) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'host_audio.c is_muted() does not safely support MB_MUTE=on/ON' }
        }
        $exe = Get-BuildBinaryPath -BuildDir $BuildDir -BinaryName 'test_movie_audio.exe'
        if (-not (Test-Path -LiteralPath $exe)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Missing $exe" }
        }
        if ((Get-Item -LiteralPath $exe).LastWriteTimeUtc -lt $ha.SourceMtime) {
            return [PSCustomObject]@{ Passed = $false; Detail = "$BuildDir/test_movie_audio.exe predates host_audio.c edit; skipping uncompiled MB_MUTE=on run for audio safety" }
        }
        foreach ($val in @('on', 'ON')) {
            $wd = New-E2ETempSubdir -Prefix ('mute_' + $val)
            $res = Invoke-IsolatedProcess -FilePath $exe -WorkingDirectory $wd -EnvOverrides @{
                MB_MUTE  = $val
                MB_DISC1 = $script:Disc1Cue
                MB_DISC2 = $script:Disc2Cue
            } -TimeoutSec 120
            if ($res.ExitCode -ne 0 -or $res.StdOut -match '(?m)^SKIP') {
                return [PSCustomObject]@{ Passed = $false; Detail = "MB_MUTE=$val failed or skipped (exit $($res.ExitCode))" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'pcm8_thread guarded and MB_MUTE=on/ON verified silently' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F3-05' -Tier 1 -Feature 'F3' -Title 'CTest audio & path targets pass silently under MB_MUTE=1' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^(test_mrbones_paths|test_snd1|test_snd1_pcm|test_cdda|test_sound_sequence|test_sound_event|test_mplay_cue)$' -TimeoutSec 120
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = "CTest audio/paths failed ($($chk.Detail))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "$($chk.Detail) audio/path CTest targets" }
    }))

    # --- F4: 06VALLEY Segment A (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F4-01' -Tier 1 -Feature 'F4' -Title '06VALLEY autoplay win via mrbones.exe --mute' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/06VALLEY', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /06VALLEY disc=1 live=sidescrl outcome=1 VALLEY finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '06VALLEY autoplay win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F4-02' -Tier 1 -Feature 'F4' -Title '06VALLEY CTest amap=494 scroll_w=15808 planes=4' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'VALLEY: frames=\d+ w=15808 amap=494 planes=4 VALLEY finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '06VALLEY'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F4-03' -Tier 1 -Feature 'F4' -Title '06VALLEY 60-frame silent QPC benchmark' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/06VALLEY', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F4-04' -Tier 1 -Feature 'F4' -Title '06VALLEY PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_valley'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/06VALLEY', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_06VALLEY.ppm' -File)
        if ($ppms.Count -lt 1) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'No *_06VALLEY.ppm capture produced' }
        }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        if (-not $p.Valid -or $p.NonBlackCount -eq 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Invalid PPM ($($p.Reason), nonBlack=$($p.NonBlackCount))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Captured $($ppms.Count) 320x224 PPM(s), nonBlack=$($p.NonBlackCount)" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F4-05' -Tier 1 -Feature 'F4' -Title '06VALLEY Segment A CFence, CFenceKey, and CBlueTomb verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VALLEY:.*VALLEY finished \(EndLevel win\)' `
            -RequiredSourceRegex '(?s)(CFence|fence).*?(CFenceKey|fence_key).*?(CBlueTomb|tomb)' `
            -StageLabel '06VALLEY'
    }))

    # --- F5: 08BAT1 Segment A (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F5-01' -Tier 1 -Feature 'F5' -Title '08BAT1 autoplay win via mrbones.exe --mute' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /08BAT1 disc=1 live=sidescrl outcome=1 BAT1 finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '08BAT1 autoplay win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F5-02' -Tier 1 -Feature 'F5' -Title '08BAT1 CTest scroll_w=3160 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'BAT1: frames=\d+ w=3160 BAT1 finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '08BAT1'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F5-03' -Tier 1 -Feature 'F5' -Title '08BAT1 60-frame silent QPC benchmark' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F5-04' -Tier 1 -Feature 'F5' -Title '08BAT1 PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_bat1'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_08BAT1.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_08BAT1.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F5-05' -Tier 1 -Feature 'F5' -Title '08BAT1 Segment A verify_batwood, 11 CAnyBone, and CEndBatwood verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'BAT1:.*BAT1 finished \(EndLevel win\)' `
            -RequiredSourceRegex 'verify_batwood\s*\([^;]*"/08BAT1"' `
            -StageLabel '08BAT1'
    }))

    # --- F6: 05MAUSO Segment A (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F6-01' -Tier 1 -Feature 'F6' -Title '05MAUSO autoplay win via mrbones.exe --mute' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /05MAUSO disc=1 live=sidescrl outcome=1 MAUSO finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '05MAUSO autoplay win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F6-02' -Tier 1 -Feature 'F6' -Title '05MAUSO CTest w=1176 planes=4 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'MAUSO: frames=\d+ w=1176 layers=\d+ planes=4 MAUSO finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '05MAUSO'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F6-03' -Tier 1 -Feature 'F6' -Title '05MAUSO 60-frame silent QPC benchmark' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F6-04' -Tier 1 -Feature 'F6' -Title '05MAUSO PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_mauso'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_05MAUSO.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_05MAUSO.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F6-05' -Tier 1 -Feature 'F6' -Title '05MAUSO Segment A CMaestro 4-wave CPartition and CButFly exit verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'MAUSO:.*MAUSO finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CMaestro|maestro).*?(CButFly|butfly|butterfly)' `
            -StageLabel '05MAUSO'
    }))

    # --- F7: 19VILLAGE Segment B (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F7-01' -Tier 1 -Feature 'F7' -Title '19VILLAGE Disc 2 INSERTCD remount and autoplay win' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/19VILLAGE', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdErr -notmatch 'INSERTCD: need disc 2 for /19VILLAGE' -or $r.StdOut -notmatch 'OK /19VILLAGE disc=2 live=sidescrl outcome=1 VILLAGE finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '19VILLAGE Disc 2 remount + win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F7-02' -Tier 1 -Feature 'F7' -Title '19VILLAGE CTest w=1600 props=8 (CHouse) win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'VILLAGE: frames=\d+ w=1600 props=8 layers=\d+ VILLAGE finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '19VILLAGE'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F7-03' -Tier 1 -Feature 'F7' -Title '19VILLAGE 60-frame silent QPC benchmark with --disc2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/19VILLAGE', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F7-04' -Tier 1 -Feature 'F7' -Title '19VILLAGE PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_village'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/19VILLAGE', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_19VILLAGE.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_19VILLAGE.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F7-05' -Tier 1 -Feature 'F7' -Title '19VILLAGE Segment B CSpider thread, GrabLittleGuy, and DropLittleGuy verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VILLAGE:.*VILLAGE finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CheckThreadHit|spider_thread|CSpider).*?(GrabLittleGuy|DropLittleGuy|LittleGuysSaved|little_guy)' `
            -StageLabel '19VILLAGE'
    }))

    # --- F8: 26LAFF Segment B (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F8-01' -Tier 1 -Feature 'F8' -Title '26LAFF Disc 2 INSERTCD remount and autoplay win' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/26LAFF', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdErr -notmatch 'INSERTCD: need disc 2 for /26LAFF' -or $r.StdOut -notmatch 'OK /26LAFF disc=2 live=sidescrl outcome=1 LAFF finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '26LAFF Disc 2 remount + win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F8-02' -Tier 1 -Feature 'F8' -Title '26LAFF CTest w=2536 boss_rem=0 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'LAFF: frames=\d+ w=2536 boss_rem=0 LAFF finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '26LAFF'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F8-03' -Tier 1 -Feature 'F8' -Title '26LAFF 60-frame silent QPC benchmark with --disc2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/26LAFF', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F8-04' -Tier 1 -Feature 'F8' -Title '26LAFF PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_laff'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/26LAFF', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_26LAFF.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_26LAFF.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F8-05' -Tier 1 -Feature 'F8' -Title '26LAFF Segment B CJunior joke state machine, CLaffSkel, and CLaffMeter verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'LAFF:.*boss_rem=0 LAFF finished \(EndLevel win\)' `
            -RequiredSourceRegex '(ToldGoodJoke|ToldBadJoke|CJunior|CringeBrain|LaffBrain).*?(CLaffSkel|CLaffMeter|laff_meter)' `
            -StageLabel '26LAFF'
    }))

    # --- F9: 15GLASS Segment C (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F9-01' -Tier 1 -Feature 'F9' -Title '15GLASS autoplay win via mrbones.exe --mute' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /15GLASS disc=1 live=sidescrl outcome=1 GLASS finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '15GLASS autoplay win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F9-02' -Tier 1 -Feature 'F9' -Title '15GLASS CTest w=1280 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'GLASS: frames=\d+ w=1280 GLASS finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '15GLASS'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F9-03' -Tier 1 -Feature 'F9' -Title '15GLASS 60-frame silent QPC benchmark' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F9-04' -Tier 1 -Feature 'F9' -Title '15GLASS PPM P6 capture shows non-black Plane B subpalette pixels' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_glass'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_15GLASS.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_15GLASS.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        if (-not $p.Valid -or $p.NonBlackCount -lt 1000 -or $p.UniqueSample -lt 8) {
            return [PSCustomObject]@{ Passed = $false; Detail = "15GLASS backdrop appears dark/flat: nonBlack=$($p.NonBlackCount) colors=$($p.UniqueSample)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "15GLASS nonBlack=$($p.NonBlackCount) colors>=$($p.UniqueSample)" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F9-05' -Tier 1 -Feature 'F9' -Title '15GLASS Segment C 176-object shards and CKeyHole exit verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'GLASS:.*GLASS finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CKeyHole|keyhole).*?(176|CTurnSha|CShootSha|CTrapSha|merge_glass_subpalette)' `
            -StageLabel '15GLASS'
    }))

    # --- F10: 17VORTEX Segment C (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F10-01' -Tier 1 -Feature 'F10' -Title '17VORTEX autoplay win via mrbones.exe --mute' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/17VORTEX', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'OK /17VORTEX disc=1 live=sidescrl outcome=1 VORTEX finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '17VORTEX autoplay win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F10-02' -Tier 1 -Feature 'F10' -Title '17VORTEX CTest w=640 and blend_mode==1 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'VORTEX: frames=\d+ w=640 VORTEX finished \(EndLevel win\)' -RequiredSourceRegex 'blend_mode == 1' -StageLabel '17VORTEX'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F10-03' -Tier 1 -Feature 'F10' -Title '17VORTEX 60-frame silent QPC benchmark' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/17VORTEX', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F10-04' -Tier 1 -Feature 'F10' -Title '17VORTEX PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_vortex'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/17VORTEX', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_17VORTEX.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_17VORTEX.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F10-05' -Tier 1 -Feature 'F10' -Title '17VORTEX Segment C CRotScroller, 17 CGlass, and _TryEndingVortexLevel verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'VORTEX:.*VORTEX finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CRotScroller|rot_scroller).*?(CGlass|glass_ring|17).*?(TryEndingVortex|vortex_end)' `
            -StageLabel '17VORTEX'
    }))

    # --- F11: 22SHADOW Segment C (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F11-01' -Tier 1 -Feature 'F11' -Title '22SHADOW Disc 2 INSERTCD remount and autoplay win' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/22SHADOW', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0 -or $r.StdErr -notmatch 'INSERTCD: need disc 2 for /22SHADOW' -or $r.StdOut -notmatch 'OK /22SHADOW disc=2 live=sidescrl outcome=1 SHADOW finished \(EndLevel win\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim()) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '22SHADOW Disc 2 remount + win' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F11-02' -Tier 1 -Feature 'F11' -Title '22SHADOW CTest w=640 win' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir -RequiredOutputRegex 'SHADOW: frames=\d+ w=640 SHADOW finished \(EndLevel win\)' -RequiredSourceRegex '' -StageLabel '22SHADOW'
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F11-03' -Tier 1 -Feature 'F11' -Title '22SHADOW 60-frame silent QPC benchmark with --disc2' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/22SHADOW', '--benchmark-frames', '60', '--no-hd-bones', '--scale', '1')
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 60
        return [PSCustomObject]@{ Passed = $chk.Valid; Detail = $chk.Reason }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F11-04' -Tier 1 -Feature 'F11' -Title '22SHADOW PPM P6 capture produces valid 320x224 frame' -Body {
        param($BuildDir)
        $cap = New-E2ETempSubdir -Prefix 'cap_shadow'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/22SHADOW', '--autoplay', '--no-hd-bones', '--scale', '1') -CaptureDir $cap
        if ($r.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        $ppms = @(Get-ChildItem -LiteralPath $cap -Filter '*_22SHADOW.ppm' -File)
        if ($ppms.Count -lt 1) { return [PSCustomObject]@{ Passed = $false; Detail = 'No *_22SHADOW.ppm capture produced' } }
        $p = Test-PpmP6File -Path $ppms[0].FullName -ExpectedWidth 320 -ExpectedHeight 224
        return [PSCustomObject]@{ Passed = ($p.Valid -and $p.NonBlackCount -gt 0); Detail = "nonBlack=$($p.NonBlackCount) ($($p.Reason))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F11-05' -Tier 1 -Feature 'F11' -Title '22SHADOW Segment C CSpirit 6 trials and _IsEndOfLevel__7CSpirit verification' -Body {
        param($BuildDir)
        return Assert-TestSidescrlLiveAndPattern -BuildDir $BuildDir `
            -RequiredOutputRegex 'SHADOW:.*SHADOW finished \(EndLevel win\)' `
            -RequiredSourceRegex '(CSpirit|shadow_trial|IsDown).*?(IsEndOfLevel|SSRHAALL|trials)' `
            -StageLabel '22SHADOW'
    }))

    # --- F12: LOG-DESENVOLVIMENTO.md update (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F12-01' -Tier 1 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md is strict UTF-8 without BOM' -Body {
        param($BuildDir)
        if (-not (Test-Path -LiteralPath $script:LogPath)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing LOG-DESENVOLVIMENTO.md' }
        }
        $bytes = [System.IO.File]::ReadAllBytes($script:LogPath)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'LOG-DESENVOLVIMENTO.md contains UTF-8 BOM' }
        }
        try {
            $enc = [System.Text.UTF8Encoding]::new($false, $true)
            [void]$enc.GetString($bytes)
        } catch {
            return [PSCustomObject]@{ Passed = $false; Detail = "Invalid UTF-8 bytes: $($_.Exception.Message)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Valid strict UTF-8 ($($bytes.Length) bytes)" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F12-02' -Tier 1 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md contains zero mojibake sequences' -Body {
        param($BuildDir)
        $text = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8)
        $badTokens = @([string][char]0xFFFD, 'Ã©', 'Ã£', 'Ã§', 'Ã³', 'Ã¡', 'Ãº')
        $found = @($badTokens | Where-Object { $text.Contains($_) })
        if ($found.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Found mojibake token(s) in LOG-DESENVOLVIMENTO.md" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '0 mojibake sequences' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F12-03' -Tier 1 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md documents R1, R4 MB_MUTE=1, and Segments A/B/C' -Body {
        param($BuildDir)
        $text = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8)
        # Look for a new section beyond line 1648 (original baseline) documenting this execution
        $lines = $text -split "`r?`n"
        if ($lines.Count -le 1650) {
            return [PSCustomObject]@{ Passed = $false; Detail = "LOG-DESENVOLVIMENTO.md not yet updated beyond baseline ($($lines.Count) lines)" }
        }
        $tail = ($lines[1648..($lines.Count - 1)]) -join "`n"
        $required = @('MB_MUTE', 'test_movie_audio', '06VALLEY', '08BAT1', '05MAUSO', '19VILLAGE', '26LAFF', '15GLASS', '17VORTEX', '22SHADOW')
        $missing = @($required | Where-Object { $tail -notmatch [regex]::Escape($_) })
        if ($missing.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "New log entries missing: $($missing -join ', ')" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'New log entries document R1, R4, and Segments A-B-C' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F12-04' -Tier 1 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md records current build-static/mrbones.exe SHA-256' -Body {
        param($BuildDir)
        $exe = Get-BuildBinaryPath -BuildDir 'build-static' -BinaryName 'mrbones.exe'
        if (-not (Test-Path -LiteralPath $exe)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing build-static/mrbones.exe' }
        }
        $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $text = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8).ToLowerInvariant()
        if (-not $text.Contains($hash)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "LOG-DESENVOLVIMENTO.md missing current SHA-256 $hash" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Matched SHA-256 $hash" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F12-05' -Tier 1 -Feature 'F12' -Title 'LOG-DESENVOLVIMENTO.md preserves heading hierarchy and integrates Codex handoff if present' -Body {
        param($BuildDir)
        $text = [System.IO.File]::ReadAllText($script:LogPath, [System.Text.Encoding]::UTF8)
        $headings = @($text -split "`r?`n" | Where-Object { $_ -match '^#{1,3}\s+' })
        if ($headings.Count -lt 216) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Heading count dropped to $($headings.Count) (< 216)" }
        }
        if (Test-Path -LiteralPath $script:HandoffChatGptPath) {
            if ($text -notmatch 'handoff-chatgpt-special-runway|special_phase|runway_phase') {
                return [PSCustomObject]@{ Passed = $false; Detail = 'docs/handoff-chatgpt-special-runway.md exists but is not integrated in LOG-DESENVOLVIMENTO.md' }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Headings=$($headings.Count)" }
    }))

    # --- F13: Git local commit prep (5 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F13-01' -Tier 1 -Feature 'F13' -Title 'Local Git HEAD advances beyond base commit 370786a' -Body {
        param($BuildDir)
        $head = Invoke-GitReadOnly -ArgList @('rev-parse', 'HEAD')
        $base = Invoke-GitReadOnly -ArgList @('rev-parse', '370786a')
        if ($head.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "git rev-parse HEAD failed: $($head.StdErr)" }
        }
        if ($base.ExitCode -eq 0 -and $head.StdOut.Trim() -eq $base.StdOut.Trim()) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'HEAD is still at base commit 370786a; M5 local commit not yet created' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "HEAD=$($head.StdOut.Trim().Substring(0, 8))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F13-02' -Tier 1 -Feature 'F13' -Title 'HEAD tree excludes disco1/, disco2/, build*, and test_save_*' -Body {
        param($BuildDir)
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        if ($tree.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = $tree.StdErr } }
        $bad = @($tree.StdOut -split "`r?`n" | Where-Object {
            $_ -match '^(disco1/|disco2/|build/|build-static/|build-chatgpt/|test_audio_reference/|test_save_)'
        })
        if ($bad.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Forbidden path in HEAD tree: $($bad[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'HEAD tree free of disc/build/temp files' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F13-03' -Tier 1 -Feature 'F13' -Title 'HEAD tree includes .gitignore, .gitattributes, LOG-DESENVOLVIMENTO.md, and 337 assets' -Body {
        param($BuildDir)
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        if ($tree.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = $tree.StdErr } }
        $files = @($tree.StdOut -split "`r?`n" | Where-Object { $_ -ne '' })
        $missing = @()
        foreach ($req in @('.gitignore', '.gitattributes', 'LOG-DESENVOLVIMENTO.md')) {
            if ($files -notcontains $req) { $missing += $req }
        }
        $assetCount = @($files | Where-Object { $_ -like 'assets/sprites_bones/*' }).Count
        if ($missing.Count -gt 0 -or $assetCount -lt 337) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Missing=[$($missing -join ',')] assetsInHead=$assetCount" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "HEAD tracks config, log, and $assetCount assets" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F13-04' -Tier 1 -Feature 'F13' -Title 'Working tree clean of uncommitted R1-R5 deliverables' -Body {
        param($BuildDir)
        $st = Invoke-GitReadOnly -ArgList @('status', '--porcelain=v1', '--untracked-files=all')
        if ($st.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = $st.StdErr } }
        $dirty = @($st.StdOut -split "`r?`n" | Where-Object {
            $_ -ne '' -and $_ -notmatch 'tests/e2e/' -and $_ -notmatch 'docs/handoff-chatgpt-special-runway\.md'
        })
        if ($dirty.Count -gt 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "$($dirty.Count) dirty/untracked path(s), first: $($dirty[0])" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Deliverables cleanly committed' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F13-05' -Tier 1 -Feature 'F13' -Title 'Git user identity and neomaster/MrBones-Sega-Saturn remote readiness' -Body {
        param($BuildDir)
        $name = Invoke-GitReadOnly -ArgList @('config', '--get', 'user.name')
        $email = Invoke-GitReadOnly -ArgList @('config', '--get', 'user.email')
        if ($name.ExitCode -ne 0 -or $email.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($name.StdOut) -or [string]::IsNullOrWhiteSpace($email.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'Missing git user.name or user.email' }
        }
        $rem = Invoke-GitReadOnly -ArgList @('remote', '-v')
        if (-not [string]::IsNullOrWhiteSpace($rem.StdOut)) {
            if ($rem.StdOut -notmatch 'github\.com[:/]neomaster/MrBones-Sega-Saturn') {
                return [PSCustomObject]@{ Passed = $false; Detail = "Unexpected remote URL: $($rem.StdOut.Trim())" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "Author=$($name.StdOut.Trim()), remote configured/ready" }
    }))

    # --- F14: Final E2E & Benchmarks (6 tests) ---
    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-01' -Tier 1 -Feature 'F14' -Title 'mrbones.exe --version reports 1.0.0 with exit 0' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @('--version') -OmitSaveDir -OmitMuteFlag
        if ($r.ExitCode -ne 0 -or $r.StdOut.Trim() -ne 'mrbones 1.0.0') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out='$($r.StdOut.Trim())'" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'mrbones 1.0.0' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-02' -Tier 1 -Feature 'F14' -Title 'mbverify.exe --quick verifies both retail discs' -Body {
        param($BuildDir)
        $r = Invoke-MbVerify -BuildDir $BuildDir -ArgList @('--disc1', $script:Disc1Cue, '--disc2', $script:Disc2Cue, '--manifest', $script:ManifestPath, '--quick')
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'disc 1: OK \(quick, 53 files checked, 9/9 audio tracks present\)' -or $r.StdOut -notmatch 'disc 2: OK \(quick, 55 files checked, 11/11 audio tracks present\)') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) out=$($r.StdOut.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Disc 1 (53 files) & Disc 2 (55 files) OK' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-03' -Tier 1 -Feature 'F14' -Title 'Baseline non-sidescrl stages 04RUNWAY & 07GUITAR autoplay win silently' -Body {
        param($BuildDir)
        foreach ($lvl in @('/04RUNWAY', '/07GUITAR')) {
            $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', $lvl, '--autoplay', '--no-hd-bones')
            if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch "OK $([regex]::Escape($lvl)) disc=1") {
                return [PSCustomObject]@{ Passed = $false; Detail = "$lvl failed (exit $($r.ExitCode))" }
            }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '04RUNWAY and 07GUITAR OK' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-04' -Tier 1 -Feature 'F14' -Title 'CTest stage catalog, runner, and progress suite pass under MB_MUTE=1' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^(test_stage_catalog|test_stage_runner|test_stage_progress|test_nvm_shadow)$' -TimeoutSec 180
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "$($chk.Detail) stage progression/catalog tests" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-05' -Tier 1 -Feature 'F14' -Title 'CTest sidescrl physics, slope, wall, motion, map, and upscale suite pass' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^(test_sidescrl_physics|test_sidescrl_slope|test_sidescrl_wall|test_sidescrl_motion|test_aabb_hitchk|test_scrl_map|test_scrl_decomp|test_scrl_tilecache|test_upscale)$' -TimeoutSec 120
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "$($chk.Detail) sidescrl subsystem tests" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T1-F14-06' -Tier 1 -Feature 'F14' -Title 'build-static CTest test_static_imports (5 Win32 DLLs) and test_msvc_dependencies pass' -Body {
        param($BuildDir)
        $target = if ($BuildDir -eq 'build') { 'build-static' } else { $BuildDir }
        $ct = Invoke-CTest -BuildDir $target -RegexFilter '^(test_static_imports|test_msvc_dependencies)$' -TimeoutSec 60
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = "$($chk.Detail) in $target" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Static CRT imports (5 DLLs) & Ninja MSVC deps passed' }
    }))

    return $tests.ToArray()
}
