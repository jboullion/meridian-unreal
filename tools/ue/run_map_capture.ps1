# The minimap pictures (docs/adr/0009-user-interface.md): runs a standalone game with -MRMapCapture
# (Source/.../Tests/MRMapCapture.cpp), which captures every geometry zone straight down into
# build/minimap/raw/, then styles them (tools/ui/minimap.py -> build/minimap/final/ and a review
# sheet). Import with tools/ue/import_ui.ps1.
#
#   powershell -File tools/ue/run_map_capture.ps1
#   powershell -File tools/ue/run_map_capture.ps1 -Only 300,301
param(
    [string[]]$Only = @(),
    [double]$GameHour = 12,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 900
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$log = Join-Path $repo "game\UnrealMeridian\Saved\Logs\mapcapture.log"
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=1280 -resy=720 -nosound -unattended " +
    "-MRStartZone=300 -MRGameHour=$GameHour -MRWeather=clear -MRNoLightning -MRSeason=1 -MRMapCapture " +
    "-abslog=`"$log`""
if ($Only) { $gameArgs += " -MRMapCaptureOnly=" + ($Only -join ",") }

$started = Get-Date
$p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
    exit 1
}
Get-Content $log | Select-String "MRMapCapture: " | ForEach-Object { $_.Line -replace '^.*MRMapCapture: ', '  ' }
Write-Host ("captured in {0:N0} s" -f ((Get-Date) - $started).TotalSeconds)
python (Join-Path $repo "tools\ui\minimap.py")
