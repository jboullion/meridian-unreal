# Every step the original lets you take, tried in play (Source/.../Tests/MRStepSurvey.cpp): runs an
# offline game with -MRStepSurvey, which walks probes with the player's capsule and movement across every
# crossing between two sectors that the original allows and that climbs or passes under a low ceiling.
# Writes game/UnrealMeridian/Saved/MRStepSurvey/results.csv; prints the failures and the totals.
#
#   powershell -File tools/ue/run_step_survey.ps1                     # our built zones (their real collision and props)
#   powershell -File tools/ue/run_step_survey.ps1 -Rooms all          # every reference room (the rest built at runtime)
#   powershell -File tools/ue/run_step_survey.ps1 -Rooms ke1.roo,i3.roo
param(
    [string[]]$Rooms = @("built"),
    [string]$Extra = "",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 3600
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$log = Join-Path $repo "game\UnrealMeridian\Saved\Logs\stepsurvey.log"

# -benchmark -fps=60: fixed 1/60 s frames, run as fast as the machine goes
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=640 -resy=360 -nosound -unattended -MROffline " +
    "-benchmark -fps=60 -MRGameHour=13 -MRWeather=clear -MRNoLightning -MRStepSurvey=$($Rooms -join ',') -abslog=`"$log`" $Extra"

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
Select-String -Path $log -Pattern "MRStepSurvey:" | ForEach-Object { $_.Line -replace '^.*LogMeridian: (Display|Warning): ', '' }
$done = Select-String -Path $log -Pattern "MRStepSurvey: DONE (\d+)/(\d+)" | Select-Object -Last 1
if (-not $done) { Write-Host "FAIL: no result (log: $log)"; exit 1 }
if ($done.Matches[0].Groups[1].Value -ne $done.Matches[0].Groups[2].Value) { exit 1 }
exit 0
