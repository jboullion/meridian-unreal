# Play the prop gallery (docs/adr/0007 "Prop gallery"): every prop and tree mesh beside its
# original sprite, in a game window, offline, starting at the front row. Double-click
# tools/ue/play_gallery.bat, or:
#
#   powershell -File tools/ue/play_gallery.ps1
#   powershell -File tools/ue/play_gallery.ps1 -GameHour 23                 (night)
#   powershell -File tools/ue/play_gallery.ps1 -Weather storm -Season 3    (a winter storm: snow)
#   powershell -File tools/ue/play_gallery.ps1 -Rebuild                    (after new props: regenerate the
#                                                                          gallery and run the world build first)
#
# The editor must be closed for -Rebuild's world build to run headless (or it runs in the open editor;
# stop Play-In-Editor first). Without -Rebuild nothing is built: it plays what the last build made.
param(
    # Meridian game hour (0-24, MRGameTimeSubsystem); -1 = the real clock
    [double]$GameHour = 13,
    # "clear", "storm" or "roll" (the server's daily rolls)
    [string]$Weather = "clear",
    # 0 spring, 1 summer, 2 fall, 3 winter; -1 = the clock
    [int]$Season = 1,
    [switch]$Rebuild,
    [switch]$Fullscreen,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"

if ($Rebuild) {
    Write-Host "Regenerating the gallery layout..."
    & python (Join-Path $repo "tools\environment\prop_gallery.py")
    if ($LASTEXITCODE -ne 0) { throw "prop_gallery.py failed" }
    Write-Host "Building the world (this imports any new meshes; a few minutes)..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_world.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_world.ps1 failed" }
}
if (-not (Test-Path (Join-Path $repo "data\environment\prop_gallery.json"))) {
    throw "No gallery yet: run with -Rebuild once (python tools/environment/prop_gallery.py, then build_world.ps1)"
}

$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -MRGallery"
$gameArgs += $(if ($Fullscreen) { " -fullscreen" } else { " -windowed -resx=$ResX -resy=$ResY" })
if ($GameHour -ge 0) { $gameArgs += " -MRGameHour=$GameHour" }
if ($Weather -and $Weather -ne "roll") { $gameArgs += " -MRWeather=$Weather" }
if ($Season -ge 0) { $gameArgs += " -MRSeason=$Season" }

Write-Host "Starting the prop gallery (Esc or F10 for the menu, Quit to close)..."
Start-Process -FilePath $Engine -ArgumentList $gameArgs
