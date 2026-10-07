# Sprite sync smoke test (docs/sprites.md): a dedicated server and two headless clients
# (UMRSpriteNetTest). Client A changes its look / colours / height and dances; client B must see it,
# and the server's monsters / NPCs (with their looks).
#
#   powershell -File tools/ue/run_sprite_net_test.ps1
#
# Exit code 0 when client B saw A's appearance and action.
param(
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [int]$Port = 7787,
    [int]$TimeoutSeconds = 180
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$logs = Join-Path $repo "game\MeridianRemastered\Saved\Logs"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serverLog = Join-Path $logs "spritenet-server-$stamp.log"
$logA = Join-Path $logs "spritenet-A-$stamp.log"
$logB = Join-Path $logs "spritenet-B-$stamp.log"

$srv = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
    "`"$proj`" /Game/Generated/Maps/L_World -server -nullrhi -nosound -unattended -port=$Port -abslog=`"$serverLog`""
$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    if ((Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern "Loaded \d+ zones" -Quiet)) { break }
}
Start-Sleep -Seconds 5

$a = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
    "`"$proj`" 127.0.0.1:$Port -game -nullrhi -nosound -unattended -windowed -MRSpriteNetTest=A -abslog=`"$logA`""
Start-Sleep -Seconds 3
$b = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
    "`"$proj`" 127.0.0.1:$Port -game -nullrhi -nosound -unattended -windowed -MRSpriteNetTest=B -abslog=`"$logB`""

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    if ((Test-Path $logB) -and (Select-String -Path $logB -Pattern "MRSpriteNet: DONE" -Quiet)) { break }
}
Start-Sleep -Seconds 2
foreach ($p in @($a, $b, $srv)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

foreach ($f in @($logA, $logB)) {
    if (Test-Path $f) {
        Select-String -Path $f -Pattern "MRSpriteNet" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
    }
}
$ok = (Test-Path $logB) -and (Select-String -Path $logB -Pattern "MRSpriteNet: saw .*look=test_sword skin=0 hair=12 shirt=5 pants=6 height=105% action=dance" -Quiet)
$ok = $ok -and (Select-String -Path $logB -Pattern "MRSpriteNet: monsters ([1-9]\d*), \1 with a look" -Quiet)
if ($ok) { Write-Host "PASS: client B sees client A's look, colours, height and dance, and the monsters"; exit 0 }
Write-Host "FAIL (logs: $logA, $logB, $serverLog)"; exit 1
