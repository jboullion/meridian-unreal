# Online smoke test (docs/adr/0010-meridian-servers.md): the game, standalone, plays on a Meridian
# server through its WebSocket gateway (UMRNetTest): logs in (an unknown name makes the account),
# plays or creates a character, enters a zone (built from the server's room), says a line and
# hears it back, takes an exit into another zone, fetches the room through the asset cache, finds
# itself in the players list, resynchronises, looks, uses and drops items, casts spells and rests, trades in Raza, fights in the Outskirts,
# travels into a room built at runtime, logs off to the character list and enters again, and logs off
# (DONE 58/58; 60/60 with -Create, 65/65 with -Pair, 62/62 with -Create -Death). AGENTS.md "Testing and verification" lists every check.
#
# Needs the server running. For the local Shards stack, in E:\2026_Experiments\meridian-browser:
#   npm run dev      (blakserv, the gateway on ws://localhost:8059, the game files on http://localhost:5173)
#
#   powershell -File tools/ue/run_net_test.ps1                  # the "Local (dev)" server, headless
#   powershell -File tools/ue/run_net_test.ps1 -Server Shards   # the online one (needs our Origin allowed there)
#   powershell -File tools/ue/run_net_test.ps1 -Hold 40 -Render # stay 40 s in a game window, pacing, with
#                                                               # screenshots (Saved/Screenshots/MRNet/)
#   powershell -File tools/ue/run_net_test.ps1 -Create          # a fresh account: makes a character through
#                                                               # the creator's path and checks its face
#   powershell -File tools/ue/run_net_test.ps1 -Pair            # with a second player (tools/ue/second_player.ts):
#                                                               # tells, ignoring, broadcasts, its wave, a trade
#   powershell -File tools/ue/run_net_test.ps1 -Create -Death   # also dies (the Forest of Farol's spiders), checks
#                                                               # the Underworld and walks back to Raza
#
# The default account is a test account that only exists on the local dev server. Exit code 0 on success.
param(
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [string]$Server = "Local",
    [int]$Hold = 0,
    [switch]$Render,
    [switch]$Create,
    [switch]$Death,
    # a second player (tools/ue/second_player.ts, a scripted Shards client as uenetpal) for the Pair step
    [switch]$Pair,
    [string]$User = "uenettest",
    [string]$Pass = "uenettest-local",
    [int]$TimeoutSeconds = 240
)
$TimeoutSeconds += $Hold
if ($Death) { $TimeoutSeconds += 300 }
if ($Create) {
    # an account the server hasn't seen: it is made at login, with no character yet
    $User = "uecreate" + (Get-Random -Minimum 10000 -Maximum 99999)
    $Pass = "uecreate-local"
}

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$logs = Join-Path $repo "game\MeridianRemastered\Saved\Logs"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $logs "nettest-$stamp.log"

if ($Server -eq "Local") {
    $gateway = New-Object System.Net.Sockets.TcpClient
    try { $gateway.Connect("127.0.0.1", 8059) } catch { }
    if (-not $gateway.Connected) {
        Write-Host "No gateway on localhost:8059. Start the Shards dev stack first: npm run dev (in meridian-browser)."
        exit 2
    }
    $gateway.Close()
}

$common = "-MRNetTest -MRServer=$Server -MRNetUser=$User -MRNetPass=$Pass -MRNetHold=$Hold -abslog=`"$log`""
if ($Death) { $common += " -MRNetDeath" }
$pal = $null
if ($Pair) {
    $palLog = Join-Path $logs "pal-$stamp.log"
    $pal = Start-Process -FilePath "node" -PassThru -WindowStyle Hidden -RedirectStandardOutput $palLog -RedirectStandardError "$palLog.err" `
        -ArgumentList "`"$(Join-Path $repo 'tools\ue\second_player.ts')`" --stay $TimeoutSeconds"
    $common += " -MRNetPal=Unrealpal"
}
if ($Render) {
    $exe = Join-Path (Split-Path -Parent $Engine) "UnrealEditor.exe"
    $game = Start-Process -FilePath $exe -PassThru -ArgumentList `
        "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=1600 -resy=900 -nosound -unattended -MRGameHour=13 -MRWeather=clear $common"
} else {
    $game = Start-Process -FilePath $Engine -PassThru -WindowStyle Hidden -ArgumentList `
        "`"$proj`" /Game/Generated/Maps/L_World -game -nullrhi -nosound -unattended -windowed $common"
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline -and -not $game.HasExited) {
    Start-Sleep -Seconds 2
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern "MRNetTest: DONE" -Quiet)) { break }
}
Start-Sleep -Seconds 3
Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue
if ($pal) { Stop-Process -Id $pal.Id -Force -ErrorAction SilentlyContinue }

if (Test-Path $log) {
    Select-String -Path $log -Pattern "LogMeridian.*(MRNetTest|MRNet:)" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
}
$done = if (Test-Path $log) { Select-String -Path $log -Pattern "MRNetTest: DONE (\d+)/(\d+)" | Select-Object -Last 1 } else { $null }
if ($done -and $done.Matches[0].Groups[1].Value -eq $done.Matches[0].Groups[2].Value -and [int]$done.Matches[0].Groups[2].Value -ge 10) {
    Write-Host "PASS: $($done.Matches[0].Value)"
    exit 0
}
Write-Host "FAIL (log: $log)"
exit 1
