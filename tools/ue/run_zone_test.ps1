# Network smoke test for zone travel: starts a dedicated server (editor binary, -server) with
# -MRZoneTest, connects one headless client, waits for "MRZoneTest: DONE", prints the results.
#
#   powershell -File tools/ue/run_zone_test.ps1
#
# Exit code 0 when every step passed.
param(
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [int]$Port = 7777,
    [int]$TimeoutSeconds = 180
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$logs = Join-Path $repo "game\MeridianRemastered\Saved\Logs"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serverLog = Join-Path $logs "zonetest-server-$stamp.log"
$clientLog = Join-Path $logs "zonetest-client-$stamp.log"

$srv = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
    "`"$proj`" /Game/Generated/Maps/L_World -server -nullrhi -nosound -unattended -port=$Port -MRZoneTest -abslog=`"$serverLog`""

# wait for the server to finish loading before connecting
$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    if ((Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern "Loaded \d+ zones" -Quiet)) { break }
}
Start-Sleep -Seconds 5

$cli = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
    "`"$proj`" 127.0.0.1:$Port -game -nullrhi -nosound -unattended -windowed -abslog=`"$clientLog`""

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    if (Select-String -Path $serverLog -Pattern "MRZoneTest: DONE" -Quiet) { break }
}
Stop-Process -Id $cli.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue

Select-String -Path $serverLog -Pattern "MRZoneTest|zone \d+ -> \d+|LogMeridian: (Warning|Error)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
Write-Host "server log: $serverLog"

$done = Select-String -Path $serverLog -Pattern "MRZoneTest: DONE (\d+)/(\d+)"
if ($done -and $done.Matches[0].Groups[1].Value -eq $done.Matches[0].Groups[2].Value) { exit 0 } else { exit 1 }
