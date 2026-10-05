# Environment look-dev captures (docs/adr/0003): renders every bookmark in
# data/environment/lookdev_cameras.json in a standalone game (-MRLookDev), copies the images to
# build/lookdev/<Label>/ and, with -Compare, writes a side-by-side sheet against another label.
#
#   powershell -File tools/ue/run_lookdev.ps1 -Label baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label mood1 -Compare baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label hall -Only hall_south,hall_front
#   powershell -File tools/ue/run_lookdev.ps1 -Label perf -Profile -ResX 1920 -ResY 1080
#     (-Profile: per camera, frame times with everything on / Nanite tessellation off / grass hidden,
#      plus CSV-profiler captures; summarise with python tools/lookdev/profile_report.py perf)
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Compare = "",
    [string[]]$Only = @(),
    [switch]$Profile,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 900
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$saved = Join-Path $repo "game\MeridianRemastered\Saved\Screenshots\MRLookDev\$Label"
$out = Join-Path $repo "build\lookdev\$Label"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\lookdev-$Label.log"

if (Test-Path $saved) { Remove-Item -Recurse -Force $saved }
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=$ResX -resy=$ResY -nosound -MRStartZone=300 -MRLookDev -MRLookDevLabel=$Label -abslog=`"$log`""
if ($Only) { $gameArgs += " -MRLookDevOnly=" + ($Only -join ",") }
if ($Profile) { $gameArgs += " -MRLookDevProfile" }

$p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
    exit 1
}

if (-not (Test-Path $saved)) { Write-Host "FAIL: no screenshots (log: $log)"; exit 1 }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -Force (Join-Path $saved "*.png") $out
if ($Profile) {
    Copy-Item -Force (Join-Path $saved "*.csv") $out -ErrorAction SilentlyContinue
    Select-String -Path $log -Pattern "MRLookDevProfile:" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } |
        Set-Content -Encoding utf8 (Join-Path $out "profile.log")
}
Get-ChildItem $out -Filter *.png | ForEach-Object { Write-Host $_.FullName }

if ($Compare) {
    python (Join-Path $repo "tools\lookdev\compare.py") $Compare $Label
}
