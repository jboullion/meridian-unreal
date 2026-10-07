# The UI's visual check (docs/adr/0009-user-interface.md): runs a standalone game with -MRUIShots
# (Source/.../Tests/MRUIShots.cpp), which screenshots the HUD and the inventory dialog with the UI
# on, then copies them to build/ui/shots/<Label>/ and writes a contact sheet there (sheet.png).
#
#   powershell -File tools/ue/run_ui_shots.ps1 -Label first
#   powershell -File tools/ue/run_ui_shots.ps1 -Label inn -StartZone 301
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [int]$StartZone = 300,
    [double]$GameHour = 13,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    # more game arguments, e.g. "-MRMood=raza_dusk"
    [string]$Extra = "",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 600
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$saved = Join-Path $repo "game\MeridianRemastered\Saved\Screenshots\MRUI\$Label"
$out = Join-Path $repo "build\ui\shots\$Label"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\uishots-$Label.log"

if (Test-Path $saved) { Remove-Item -Recurse -Force $saved }
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=$ResX -resy=$ResY -nosound -unattended " +
    "-MRStartZone=$StartZone -MRGameHour=$GameHour -MRWeather=clear -MRNoLightning -MRSeason=1 -MRUIShots -MRUIShotsLabel=$Label -abslog=`"$log`" $Extra"

$started = Get-Date
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
Write-Host ("captured in {0:N0} s" -f ((Get-Date) - $started).TotalSeconds)
if (-not (Test-Path $saved)) { Write-Host "FAIL: no screenshots (log: $log)"; exit 1 }
New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item (Join-Path $out "*.png") -ErrorAction SilentlyContinue
Copy-Item -Force (Join-Path $saved "*.png") $out
python (Join-Path $repo "tools\ui\shots_sheet.py") $out
