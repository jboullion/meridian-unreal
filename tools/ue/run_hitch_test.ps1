# Hitches while playing: runs an offline game with -MRHitchTour (Source/.../Tests/MRHitchTour.cpp), which
# records every frame's time from launch, waits for the warm-up screen like a player, then walks through
# Raza, the Outskirts and a few interiors, turning a full circle at each stop. Prints the summary
# (p50/p95/p99, frames over 33 and 100 ms, per stop) and writes Saved/MRHitch/<label>.csv.
#
#   powershell -File tools/ue/run_hitch_test.ps1 -Label before
#   powershell -File tools/ue/run_hitch_test.ps1 -Label after -Compare before
#   powershell -File tools/ue/run_hitch_test.ps1 -Label rain -Weather rain -Exec "r.VolumetricCloud.SkyAO 0" -Extra -MRHitchCsv
#
# -Extra -MRHitchCsv also records a CSV profile with per-pass GPU timings: python tools/lookdev/hitch_report.py
# -Extra -MRHitchMaxFPS=60 plays capped at 60 fps (uncapped is the default). An -Exec "sg.*" is saved into
# Saved/Config/WindowsEditor/GameUserSettings.ini by the game: it carries into later runs and play.
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
    [int]$TimeoutSeconds = 600
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$saved = Join-Path $repo "game\UnrealMeridian\Saved"
$log = Join-Path $saved "Logs\hitch-$Label.log"

$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=$ResX -resy=$ResY -nosound -unattended " +
    "-MRGameHourFrom=13 -MRWeather=$Weather -MRNoLightning -MRHitchTour -MRHitchLabel=$Label -abslog=`"$log`" $Extra"
if ($Exec) { $gameArgs += " -ExecCmds=`"$Exec`"" }

for ($attempt = 1; $attempt -le 2; $attempt++) {
    $p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
        exit 1
    }
    if (-not (Select-String -Path $log -Pattern "GPU crash detected" -Quiet)) { break }
    Write-Host "GPU crash (attempt $attempt)"
    if ($attempt -eq 2) { exit 1 }
}
Select-String -Path $log -Pattern "MRHitch:|MRWarmup:" | ForEach-Object { $_.Line -replace '^.*LogMeridian: Display: ', '' }
$done = Select-String -Path $log -Pattern "MRHitch: DONE" | Select-Object -Last 1
if (-not $done) { Write-Host "FAIL: no result (log: $log)"; exit 1 }

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
