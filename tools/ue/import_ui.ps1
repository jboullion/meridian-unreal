# Import the in-game UI's art (tools/ue/import_ui.py): frames, icons and minimap pictures from
# build/ui and build/minimap into /Game/Generated/UI. Run tools/ui/build_ui_art.py and
# tools/ui/build_icons.py first. Uses the open editor when there is one, else a headless editor
# (as build_world.ps1).
#
#   powershell -File tools/ue/import_ui.ps1
#   powershell -File tools/ue/import_ui.ps1 -Headless
param(
    [switch]$Headless,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)
& (Join-Path $PSScriptRoot "build_world.ps1") -Script import_ui.py -Headless:$Headless -Engine $Engine
exit $LASTEXITCODE
