# The day/night cycle (docs/adr/0005) at fixed game hours: for each hour, capture the cameras as
# look-dev label cycle_<hh> (run_lookdev.ps1 -GameHour), then write one sheet with the hours side by
# side (build/lookdev/compare_cycle_*.png). The original's phases: night < 6 and > 20, dawn 6-8,
# day 9-17, dusk 18-20.
#
#   powershell -File tools/lookdev/cycle_test.ps1
#   powershell -File tools/lookdev/cycle_test.ps1 -Hours 5,6,7,8 -Cameras hall_front
param(
    [string[]]$Hours = @("0", "6", "9", "14", "19", "22"),
    [string[]]$Cameras = @("square_overview", "west_street", "hall_front", "north_gate", "pond")
)
$ErrorActionPreference = "Stop"
$Hours = $Hours -split ","   # "a,b" through powershell -File arrives as one string
$Cameras = $Cameras -split ","
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$lookdevArgs = if ($Cameras -contains "all") { @() } else { @("-Only", ($Cameras -join ",")) }

$labels = @()
foreach ($h in $Hours) {
    $label = "cycle_" + ([double]$h).ToString("00.##", [Globalization.CultureInfo]::InvariantCulture)
    Write-Host "== $label"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\run_lookdev.ps1") -Label $label -GameHour $h @lookdevArgs | Select-Object -Last 1
    $labels += $label
}
$compareArgs = @($labels)
if (-not ($Cameras -contains "all")) { $compareArgs += @("--only", ($Cameras -join ",")) }
python (Join-Path $repo "tools\lookdev\compare.py") @compareArgs
