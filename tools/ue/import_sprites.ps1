# Import the sprite player atlases and build the sprite body materials (tools/ue/import_sprites.py).
# Run tools/sprites/build_player_sprites.py first. Uses the open editor when there is one, else a
# headless editor (as build_world.ps1).
#
#   powershell -File tools/ue/import_sprites.ps1
#   powershell -File tools/ue/import_sprites.ps1 -Headless
param(
    [switch]$Headless,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)
# atlases no longer in the layout (e.g. the per-translation ones before runtime colour): delete the
# files while no editor has them loaded
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$layout = Get-Content (Join-Path $repo "data\sprites\player_parts.json") -Raw | ConvertFrom-Json
$wanted = @($layout.atlases.PSObject.Properties | ForEach-Object { $_.Value.texture; if ($_.Value.ramp) { $_.Value.ramp } })
$dir = Join-Path $repo "game\UnrealMeridian\Content\Generated\Sprites"
if (Test-Path $dir) {
    Get-ChildItem $dir -Filter "T_Spr*_*.uasset" | Where-Object { @("T_SprXlat", "T_SprClass") -notcontains $_.BaseName } | Where-Object { $wanted -notcontains $_.BaseName } |
        ForEach-Object { Remove-Item $_.FullName; Write-Host "removed old $($_.BaseName)" }
}
& (Join-Path $PSScriptRoot "build_world.ps1") -Script import_sprites.py -Headless:$Headless -Engine $Engine
exit $LASTEXITCODE
