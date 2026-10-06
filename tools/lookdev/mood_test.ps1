# Capture the look-dev cameras under several lighting moods (data/environment/moods.json), one mood
# pinned at a time (run_lookdev.ps1 -Mood -> UMREnvironmentSubsystem -MRMood: no day/night cycle, the
# mood's own sun), as look-dev labels mood_<name>. Nothing is rebuilt or baked into the level.
#
#   powershell -File tools/lookdev/mood_test.ps1 -Moods raza_morning,raza_afternoon,raza_dusk,raza_night
param(
    [string[]]$Moods = @("raza_morning", "raza_afternoon", "raza_dusk", "raza_night"),
    [string[]]$Cameras = @("square_overview", "west_street", "inn_front", "hall_front", "tavern_front", "pond",
                           "temple_front", "north_gate")
)
$ErrorActionPreference = "Stop"
$Moods = $Moods -split ","   # "a,b" through powershell -File arrives as one string
$Cameras = $Cameras -split ","
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$lookdevArgs = if ($Cameras -contains "all") { @() } else { @("-Only", ($Cameras -join ",")) }

foreach ($m in $Moods) {
    Write-Host "== mood_$m"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\run_lookdev.ps1") -Label "mood_$m" -Mood $m @lookdevArgs | Select-Object -Last 1
}
