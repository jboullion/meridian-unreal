# Capture the look-dev cameras under several lighting moods (data/environment/moods.json). For each
# mood: apply it to L_World (tools/ue/zone_mood.py with MR_MOOD), capture as look-dev label
# mood_<name>. Afterwards the level's own mood (moods.json "levels") is applied again.
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

function Apply-Mood {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\build_world.ps1") -Script zone_mood.py | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "zone_mood.py failed" }
}

try {
    foreach ($m in $Moods) {
        Write-Host "== mood_$m"
        $env:MR_MOOD = $m
        Apply-Mood
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\run_lookdev.ps1") -Label "mood_$m" @lookdevArgs | Select-Object -Last 1
    }
}
finally {
    Write-Host "== restoring the level's mood"
    Remove-Item Env:\MR_MOOD -ErrorAction SilentlyContinue
    Apply-Mood
}
