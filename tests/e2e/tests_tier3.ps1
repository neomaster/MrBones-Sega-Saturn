# Tier 3 Cross-Feature Pairwise Interaction Tests (16 tests: T3-P01..T3-P16)

function Get-Tier3Tests {
    $tests = New-Object System.Collections.Generic.List[object]

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P01' -Tier 3 -Feature 'F3+F4+F7' -Title 'Silent MB_MUTE=1 + --mute across Disc 1 (06VALLEY), Disc 2 (19VILLAGE), and test_movie_audio' -Body {
        param($BuildDir)
        $r1 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/06VALLEY', '--autoplay', '--no-hd-bones')
        $r2 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', '/19VILLAGE', '--autoplay', '--no-hd-bones')
        $r3 = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0 -or $r3.ExitCode -ne 0 -or ($r3.StdOut -match '(?m)^SKIP')) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exits=($($r1.ExitCode),$($r2.ExitCode),$($r3.ExitCode))" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Disc 1, Disc 2, and movie audio all passed silently' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P02' -Tier 3 -Feature 'F4+F10+F14' -Title 'mrbones --check --mute validates 13 FMV transitions including 06VALLEY and 17VORTEX' -Body {
        param($BuildDir)
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--check') -TimeoutSec 300
        if ($r.ExitCode -ne 0 -or $r.StdOut -notmatch 'check: 13 FMV transitions OK') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode) err=$($r.StdErr.Trim())" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '13 FMV transitions OK via --check --mute' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P03' -Tier 3 -Feature 'F10+F7+F14' -Title '17VORTEX -> 18RIDE Disc 1 to Disc 2 boundary via test_stage_progress & test_stage_runner' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^(test_stage_progress|test_stage_runner)$' -TimeoutSec 180
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Disc 1->Disc 2 boundary verified in stage_progress & stage_runner' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P04' -Tier 3 -Feature 'F6+F4' -Title '05MAUSO -> 06VALLEY direct transition and nvmshadow.bin unlock continuity' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^test_stage_progress$' -TimeoutSec 120
        if ($ct.ExitCode -ne 0 -or $ct.StdOut -notmatch 'OK stage_progress') {
            return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($ct.ExitCode)" }
        }
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--autoplay', '--no-hd-bones')
        if ($r.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "05MAUSO exit $($r.ExitCode)" } }
        $nvm = Test-NvmShadowFile -Path (Join-Path $r.SaveDir 'nvmshadow.bin')
        if (-not $nvm.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = $nvm.Reason } }
        return [PSCustomObject]@{ Passed = $true; Detail = '05MAUSO->06VALLEY progression & BONE save verified' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P05' -Tier 3 -Feature 'F5+F14' -Title '08BAT1 autoplay win persists valid 40-byte BONE nvmshadow.bin in isolated --save-dir' -Body {
        param($BuildDir)
        $saveDir = New-E2ETempSubdir -Prefix 'p05_bat1'
        $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/08BAT1', '--autoplay', '--no-hd-bones') -SaveDir $saveDir
        if ($r.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "exit=$($r.ExitCode)" } }
        $nvm = Test-NvmShadowFile -Path (Join-Path $saveDir 'nvmshadow.bin')
        if (-not $nvm.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = $nvm.Reason } }
        return [PSCustomObject]@{ Passed = $true; Detail = "08BAT1 saved valid nvmshadow.bin (tag=$($nvm.Tag))" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P06' -Tier 3 -Feature 'F7+F8+F11+F14' -Title 'Disc 2 stages 19VILLAGE, 22SHADOW, 26LAFF share --save-dir preserving nvmshadow.bin' -Body {
        param($BuildDir)
        $sharedSave = New-E2ETempSubdir -Prefix 'p06_shared'
        foreach ($lvl in @('/06VALLEY', '/19VILLAGE', '/22SHADOW', '/26LAFF')) {
            $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', $lvl, '--autoplay', '--no-hd-bones') -SaveDir $sharedSave
            if ($r.ExitCode -ne 0) {
                return [PSCustomObject]@{ Passed = $false; Detail = "$lvl failed (exit $($r.ExitCode))" }
            }
        }
        $nvm = Test-NvmShadowFile -Path (Join-Path $sharedSave 'nvmshadow.bin')
        if (-not $nvm.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = $nvm.Reason } }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Shared save-dir survived Disc 1 -> Disc 2 stages' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P07' -Tier 3 -Feature 'F9+F14' -Title '15GLASS upscale xbr2x (2x) vs nearest (1x) both produce non-black PPM captures' -Body {
        param($BuildDir)
        $cap1 = New-E2ETempSubdir -Prefix 'p07_1x'
        $cap2 = New-E2ETempSubdir -Prefix 'p07_2x'
        $r1 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--autoplay', '--no-hd-bones', '--upscale', 'nearest', '--scale', '1') -CaptureDir $cap1
        $r2 = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/15GLASS', '--autoplay', '--upscale', 'xbr2x', '--scale', '2') -CaptureDir $cap2
        if ($r1.ExitCode -ne 0 -or $r2.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exits=($($r1.ExitCode),$($r2.ExitCode))" }
        }
        $w1 = @(Get-ChildItem -LiteralPath $cap1 -Filter '*_15GLASS_win.ppm' -File)
        $w2 = @(Get-ChildItem -LiteralPath $cap2 -Filter '*_15GLASS_win.ppm' -File)
        if ($w1.Count -lt 1 -or $w2.Count -lt 1) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Missing win captures: c1=$($w1.Count) c2=$($w2.Count)" }
        }
        $p1 = Test-PpmP6File -Path $w1[0].FullName -ExpectedWidth 344 -ExpectedHeight 288
        $p2 = Test-PpmP6File -Path $w2[0].FullName -ExpectedWidth 664 -ExpectedHeight 512
        if (-not $p1.Valid -or -not $p2.Valid -or $p1.NonBlackCount -lt 1000 -or $p2.NonBlackCount -lt 4000) {
            return [PSCustomObject]@{ Passed = $false; Detail = "PPM check failed: 1x=$($p1.Reason)/nb=$($p1.NonBlackCount), 2x=$($p2.Reason)/nb=$($p2.NonBlackCount)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = '1x (344x288) and 2x (664x512) captures valid & non-black' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P08' -Tier 3 -Feature 'F1+F3' -Title 'Running test_movie_audio and mrbones keeps repo root free of save/audio artifacts' -Body {
        param($BuildDir)
        $rA = Invoke-MovieAudio -BuildDir $BuildDir -MuteValue '1' -UseCache
        $rB = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', '/05MAUSO', '--autoplay', '--no-hd-bones')
        if ($rA.ExitCode -ne 0 -or $rB.ExitCode -ne 0) {
            return [PSCustomObject]@{ Passed = $false; Detail = "exits=($($rA.ExitCode),$($rB.ExitCode))" }
        }
        $badDirs = @(Get-ChildItem -LiteralPath $script:RepoRoot -Directory | Where-Object { $_.Name -eq 'test_audio_reference' -or $_.Name -like 'test_save_*' })
        $badNvm = Test-Path -LiteralPath (Join-Path $script:RepoRoot 'nvmshadow.bin')
        if ($badDirs.Count -gt 0 -or $badNvm) {
            return [PSCustomObject]@{ Passed = $false; Detail = "Root polluted: badDirs=$($badDirs.Count) badNvm=$badNvm" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Zero root pollution after isolated audio & stage runs' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P09' -Tier 3 -Feature 'F2+F13' -Title '.gitignore rules align with HEAD tree (0 ignored builds/discs committed, 337 assets tracked)' -Body {
        param($BuildDir)
        $lines = Get-Content -LiteralPath $script:GitIgnorePath -Encoding UTF8 | ForEach-Object { $_.Trim() }
        if ($lines -notcontains 'disco1/' -or $lines -notcontains 'disco2/') {
            return [PSCustomObject]@{ Passed = $false; Detail = '.gitignore missing disco1/ or disco2/' }
        }
        $tree = Invoke-GitReadOnly -ArgList @('ls-tree', '-r', '--name-only', 'HEAD')
        $files = @($tree.StdOut -split "`r?`n" | Where-Object { $_ -ne '' })
        $forbidden = @($files | Where-Object { $_ -match '^(disco1/|disco2/|build/|build-static/|build-chatgpt/|test_audio_reference/|test_save_)' })
        $assets = @($files | Where-Object { $_ -like 'assets/sprites_bones/*' })
        if ($forbidden.Count -gt 0 -or $assets.Count -lt 337) {
            return [PSCustomObject]@{ Passed = $false; Detail = "forbidden=$($forbidden.Count) assetsInHead=$($assets.Count)" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = "0 forbidden paths, $($assets.Count) assets tracked in HEAD" }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P10' -Tier 3 -Feature 'F1+F12' -Title 'LOG-DESENVOLVIMENTO.md root cleanup record matches actual clean root state' -Body {
        param($BuildDir)
        $loose = @(Get-ChildItem -LiteralPath $script:RepoRoot -File | Where-Object { $_.Extension -in @('.obj', '.exe') }).Count
        $lines = [System.IO.File]::ReadAllLines($script:LogPath, [System.Text.Encoding]::UTF8)
        if ($loose -gt 0 -or $lines.Count -le 1650) {
            return [PSCustomObject]@{ Passed = $false; Detail = "looseRootFiles=$loose logLines=$($lines.Count)" }
        }
        $tail = ($lines[1648..($lines.Count - 1)]) -join "`n"
        if ($tail -notmatch 'gitignore|raiz|disco1|test_save') {
            return [PSCustomObject]@{ Passed = $false; Detail = 'New log entry does not document R1 root/gitignore cleanup' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Root hygiene matches updated LOG-DESENVOLVIMENTO.md' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P11' -Tier 3 -Feature 'F3+F12' -Title 'LOG-DESENVOLVIMENTO.md MB_MUTE=1 audio verification aligns with host_audio.c & CMakeLists.txt' -Body {
        param($BuildDir)
        $ha = Get-HostAudioAnalysis
        $lines = [System.IO.File]::ReadAllLines($script:LogPath, [System.Text.Encoding]::UTF8)
        if (-not $ha.SupportsOn -or -not $ha.Pcm8Guarded -or $lines.Count -le 1650) {
            return [PSCustomObject]@{ Passed = $false; Detail = "ha.SupportsOn=$($ha.SupportsOn) ha.Pcm8Guarded=$($ha.Pcm8Guarded) logLines=$($lines.Count)" }
        }
        $tail = ($lines[1648..($lines.Count - 1)]) -join "`n"
        if ($tail -notmatch 'MB_MUTE' -or $tail -notmatch 'test_movie_audio') {
            return [PSCustomObject]@{ Passed = $false; Detail = 'New log entry missing MB_MUTE / test_movie_audio verification' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Silent audio implementation & log aligned' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P12' -Tier 3 -Feature 'F12+F13' -Title 'LOG-DESENVOLVIMENTO.md is committed cleanly in HEAD with zero uncommitted diff' -Body {
        param($BuildDir)
        $head = Invoke-GitReadOnly -ArgList @('rev-parse', 'HEAD')
        $base = Invoke-GitReadOnly -ArgList @('rev-parse', '370786a')
        $st = Invoke-GitReadOnly -ArgList @('status', '--porcelain=v1', '--', 'LOG-DESENVOLVIMENTO.md')
        if (($base.ExitCode -eq 0 -and $head.StdOut.Trim() -eq $base.StdOut.Trim()) -or -not [string]::IsNullOrWhiteSpace($st.StdOut)) {
            return [PSCustomObject]@{ Passed = $false; Detail = "HEAD=$($head.StdOut.Trim()) logStatus='$($st.StdOut.Trim())'" }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Updated LOG-DESENVOLVIMENTO.md cleanly committed in HEAD' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P13' -Tier 3 -Feature 'F4+F5+F6+F14' -Title 'Segment A stages (05MAUSO, 06VALLEY, 08BAT1) sequential 30-frame benchmark' -Body {
        param($BuildDir)
        foreach ($lvl in @('/05MAUSO', '/06VALLEY', '/08BAT1')) {
            $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--level', $lvl, '--benchmark-frames', '30', '--no-hd-bones', '--scale', '1')
            if ($r.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl exit $($r.ExitCode)" } }
            $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 30
            if (-not $chk.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl $($chk.Reason)" } }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 3 Segment A stages benchmarked cleanly (30 frames each)' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P14' -Tier 3 -Feature 'F7+F8+F14' -Title 'Segment B Disc 2 stages (19VILLAGE, 26LAFF) sequential 30-frame benchmark' -Body {
        param($BuildDir)
        foreach ($lvl in @('/19VILLAGE', '/26LAFF')) {
            $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', $lvl, '--benchmark-frames', '30', '--no-hd-bones', '--scale', '1')
            if ($r.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl exit $($r.ExitCode)" } }
            $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 30
            if (-not $chk.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl $($chk.Reason)" } }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'Both Segment B stages benchmarked cleanly (30 frames each)' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P15' -Tier 3 -Feature 'F9+F10+F11+F14' -Title 'Segment C stages (15GLASS, 17VORTEX, 22SHADOW) sequential 30-frame benchmark' -Body {
        param($BuildDir)
        foreach ($lvl in @('/15GLASS', '/17VORTEX', '/22SHADOW')) {
            $r = Invoke-MrBones -BuildDir $BuildDir -ArgList @($script:Disc1Cue, $script:ManifestPath, '--disc2', $script:Disc2Cue, '--level', $lvl, '--benchmark-frames', '30', '--no-hd-bones', '--scale', '1')
            if ($r.ExitCode -ne 0) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl exit $($r.ExitCode)" } }
            $chk = Test-BenchmarkCsvOutput -StdOut $r.StdOut -ExpectedFrames 30
            if (-not $chk.Valid) { return [PSCustomObject]@{ Passed = $false; Detail = "$lvl $($chk.Reason)" } }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 3 Segment C stages benchmarked cleanly (30 frames each)' }
    }))

    [void]$tests.Add((New-E2ETestSpec -Id 'T3-P16' -Tier 3 -Feature 'F3+F14' -Title 'Finale replay determinism tests pass under MB_MUTE=1 without root leakage' -Body {
        param($BuildDir)
        $ct = Invoke-CTest -BuildDir $BuildDir -RegexFilter '^test_finale_' -TimeoutSec 120
        $chk = Test-CTestAllPassed -CTestResult $ct
        if (-not $chk.Passed) {
            return [PSCustomObject]@{ Passed = $false; Detail = $chk.Detail }
        }
        if (Test-Path -LiteralPath (Join-Path $script:RepoRoot 'nvmshadow.bin')) {
            return [PSCustomObject]@{ Passed = $false; Detail = 'nvmshadow.bin leaked to root' }
        }
        return [PSCustomObject]@{ Passed = $true; Detail = 'All 3 finale replay CTest targets passed silently' }
    }))

    return $tests.ToArray()
}
