# Hitches while playing: runs an offline game with -MRHitchTour (Source/.../Tests/MRHitchTour.cpp), which
# records every frame's time from launch, waits for the warm-up screen like a player, then walks through
# Raza, the Outskirts and a few interiors, turning a full circle at each stop, and lingers 75 s at the end
# (zone unloads, GC). Prints the summary (p50/p95/p99, frames over 33 and 100 ms, per stop) and writes
# Saved/MRHitch/<label>.csv.
#
#   powershell -File tools/ue/run_hitch_test.ps1 -Label before
#   powershell -File tools/ue/run_hitch_test.ps1 -Label after -Compare before
#   powershell -File tools/ue/run_hitch_test.ps1 -Label rain -Weather rain -Exec "r.VolumetricCloud.SkyAO 0" -Extra -MRHitchCsv
#
# -Extra -MRHitchCsv also records a CSV profile with per-pass GPU timings: python tools/lookdev/hitch_report.py
# -Extra -MRHitchDump logs the stat stack of every frame over 40 ms ("Thread Hitch" in the log).
# -Extra -MRHitchMaxFPS=60 plays capped at 60 fps (uncapped is the default). An -Exec "sg.*" is saved into
# Saved/Config/WindowsEditor/GameUserSettings.ini by the game: it carries into later runs and play.
#
# -Packaged <dir> runs a packaged game instead (tools/ue/package.ps1 -Config Development -OutName Win64-dev;
# Shipping writes no log). It plays with the player's own settings (%LOCALAPPDATA%\UnrealMeridian\Saved\Config\
# Windows\GameUserSettings.ini), which are put back after the run; -CleanConfig hides them, so the project's
# defaults play.
#   powershell -File tools/ue/run_hitch_test.ps1 -Packaged build/package/Win64-dev -CleanConfig -Label pkg_default
#
# After the summary it counts, per stop, the log's PSO precache misses, loading flushes, garbage collections
# and (with -MRHitchDump) hitches.
#
# The first run after a shader or material change also measures the warm-up; run twice to see a warm start.
param(
    [string]$Label = "latest",
    [string]$Compare = "",
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Weather = "clear",
    [string]$Exec = "",
    [string]$Extra = "",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [string]$Packaged = "",
    [switch]$CleanConfig,
    [int]$TimeoutSeconds = 600
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$saved = Join-Path $repo "game\UnrealMeridian\Saved"
$log = Join-Path $saved "Logs\hitch-$Label.log"

$tourArgs = "-windowed -resx=$ResX -resy=$ResY -nosound -unattended " +
    "-MRGameHourFrom=13 -MRWeather=$Weather -MRNoLightning -MRHitchTour -MRHitchLabel=$Label -abslog=`"$log`" $Extra"
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game $tourArgs"
$userConfig = Join-Path $env:LOCALAPPDATA "UnrealMeridian\Saved\Config\Windows\GameUserSettings.ini"
$configBackup = "$userConfig.hitchtest"
if ($Packaged) {
    $exe = Join-Path (Resolve-Path $Packaged) "UnrealMeridian.exe"
    if (-not (Test-Path $exe)) { Write-Host "FAIL: no $exe"; exit 1 }
    $Engine = $exe
    $gameArgs = $tourArgs
    # the packaged game saves its settings on quit: keep the player's, and hide them for -CleanConfig
    if (Test-Path $userConfig) { Copy-Item $userConfig $configBackup -Force }
    if ($CleanConfig -and (Test-Path $userConfig)) { Remove-Item $userConfig }
}
if ($Exec) { $gameArgs += " -ExecCmds=`"$Exec`"" }

$failed = $false
try {
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        $p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
        if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
            $failed = $true
            break
        }
        if (-not (Select-String -Path $log -Pattern "GPU crash detected" -Quiet)) { break }
        Write-Host "GPU crash (attempt $attempt)"
        if ($attempt -eq 2) { $failed = $true }
    }
} finally {
    if ($Packaged) {
        if (Test-Path $configBackup) { Move-Item $configBackup $userConfig -Force }
        elseif (Test-Path $userConfig) { Remove-Item $userConfig }
    }
}
if ($failed) { exit 1 }
Select-String -Path $log -Pattern "MRHitch:|MRWarmup:" | Where-Object { $_.Line -notmatch "MRHitch: at " } |
    ForEach-Object { $_.Line -replace '^.*LogMeridian: Display: ', '' }
$done = Select-String -Path $log -Pattern "MRHitch: DONE" | Select-Object -Last 1
if (-not $done) { Write-Host "FAIL: no result (log: $log)"; exit 1 }

# what the log says happened at each stop ("MRHitch: at <stop>" marks them; before the first: the warm-up)
$kinds = [ordered]@{ "pso_miss" = "PSOPrecacheState: Missed"; "flush" = "FlushAsyncLoading"; "gc" = "GC purged"; "hitch" = "Thread Hitch" }
$counts = [ordered]@{}
$stop = "warmup"
foreach ($line in Get-Content $log) {
    if ($line -match "MRHitch: at (\S+)") { $stop = $Matches[1]; continue }
    if ($line -match "MRHitch: DONE") { break }
    foreach ($k in $kinds.Keys) {
        if ($line.Contains($kinds[$k])) {
            if (-not $counts.Contains($stop)) { $counts[$stop] = [ordered]@{ "pso_miss" = 0; "flush" = 0; "gc" = 0; "hitch" = 0 } }
            $counts[$stop][$k]++
        }
    }
}
Write-Host ""
Write-Host ("{0,-14} {1,9} {2,6} {3,4} {4,6}" -f "log by stop", "pso_miss", "flush", "gc", "hitch")
foreach ($s in $counts.Keys) {
    $c = $counts[$s]
    Write-Host ("{0,-14} {1,9} {2,6} {3,4} {4,6}" -f $s, $c["pso_miss"], $c["flush"], $c["gc"], $c["hitch"])
}

if ($Compare) {
    $before = Join-Path $saved "Logs\hitch-$Compare.log"
    if (Test-Path $before) {
        Write-Host ""
        Write-Host "--- $Compare"
        Select-String -Path $before -Pattern "MRHitch: DONE" | Select-Object -Last 1 | ForEach-Object { $_.Line -replace '^.*LogMeridian: Display: ', '' }
    } else {
        Write-Host "(no log for $Compare)"
    }
}
exit 0
