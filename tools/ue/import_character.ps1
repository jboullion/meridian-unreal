# Import a MakeHuman character kit into the project (see tools/ue/import_character.py).
#
#   powershell -File tools/ue/import_character.ps1 -Kit art_src/characters/MPFB_Male
#   powershell -File tools/ue/import_character.ps1 -Kit art_src/characters/MPFB_Female -RebuildMaterials
#
# Close the editor first (assets are written by a separate headless editor session).
param(
    [Parameter(Mandatory = $true)][string]$Kit,
    [switch]$RebuildMaterials,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\import_character.log"

$env:MR_CHAR_KIT = (Resolve-Path $Kit).Path
$env:MR_REBUILD_MATERIALS = if ($RebuildMaterials) { "1" } else { "" }
& $Engine $proj "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'import_character.py')" -unattended -nosplash -RenderOffscreen "-abslog=$log" | Out-Null

Select-String -Path $log -Pattern "\[import_character\]" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
if (Select-String -Path $log -Pattern "import_character\] FAILED" -Quiet) { exit 1 }
