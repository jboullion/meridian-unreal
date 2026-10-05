# Environment look-dev captures (docs/adr/0003): renders every bookmark in
# data/environment/lookdev_cameras.json in a standalone game (-MRLookDev), copies the images to
# build/lookdev/<Label>/ and, with -Compare, writes a side-by-side sheet against another label.
#
#   powershell -File tools/ue/run_lookdev.ps1 -Label baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label mood1 -Compare baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label hall -Only hall_south,hall_front
#   powershell -File tools/ue/run_lookdev.ps1 -Label slow -Settle 3       (fixed 3 s per camera, the old way)
#     (by default each camera is saved as soon as its image settles, with clouds and wind frozen;
#      see Source/.../Tests/MRLookDevTour.h)
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
    [double]$Settle = 0,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 900
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$saved = Join-Path $repo "game\MeridianRemastered\Saved\Screenshots\MRLookDev\$Label"
$out = Join-Path $repo "build\lookdev\$Label"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\lookdev-$Label.log"

if (Test-Path $saved) { Remove-Item -Recurse -Force $saved }
# -unattended: a GPU crash exits instead of waiting on an error dialog (then we try once more)
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=$ResX -resy=$ResY -nosound -unattended -MRStartZone=300 -MRLookDev -MRLookDevLabel=$Label -abslog=`"$log`""
if ($Only) { $gameArgs += " -MRLookDevOnly=" + ($Only -join ",") }
if ($Profile) { $gameArgs += " -MRLookDevProfile" }
if ($Settle -gt 0) { $gameArgs += " -MRLookDevSettle=$Settle" }

$started = Get-Date
for ($attempt = 1; $attempt -le 2; $attempt++) {
    $p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
        exit 1
    }
    if (-not (Select-String -Path $log -Pattern "GPU crash detected" -Quiet)) { break }
    $dump = Join-Path (Split-Path $log) ("lookdev-$Label-gpucrash-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
    Copy-Item $log $dump
    Write-Host "GPU crash (attempt $attempt; log kept as $dump)"
    if ($attempt -eq 2) { exit 1 }
}
Get-Content $log | Select-String "MRLookDev: .*\.png \(" | ForEach-Object { $_.Line -replace '^.*[\\/]', '  ' }
Write-Host ("captured in {0:N0} s" -f ((Get-Date) - $started).TotalSeconds)

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
