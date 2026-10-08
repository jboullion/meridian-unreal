# The original's movement, checked in play (docs/findings.md "Walking"): runs an offline game with
# -MRMoveTest (Source/.../Tests/MRMoveTest.cpp), which measures the run and walk speeds, runs off a
# ledge across a 2.5 m and a 5 m gap, and climbs a 0.72 m step in the Mausoleum. Exits 0 when every
# step passes.
#
#   powershell -File tools/ue/run_move_test.ps1
#   powershell -File tools/ue/run_move_test.ps1 -Extra "-ExecCmds=`"mr.Move.SpeedScale 0.5`""
param(
    [string]$Extra = "",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 300
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\movetest.log"

$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=960 -resy=540 -nosound -unattended " +
    "-MRStartZone=306 -MRGameHour=13 -MRWeather=clear -MRNoLightning -MRMoveTest -abslog=`"$log`" $Extra"

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
Select-String -Path $log -Pattern "MRMoveTest:" | ForEach-Object { $_.Line -replace '^.*LogMeridian: Display: ', '' }
$done = Select-String -Path $log -Pattern "MRMoveTest: DONE (\d+)/(\d+)" | Select-Object -Last 1
if (-not $done) { Write-Host "FAIL: no result (log: $log)"; exit 1 }
if ($done.Matches[0].Groups[1].Value -ne $done.Matches[0].Groups[2].Value) { exit 1 }
exit 0
