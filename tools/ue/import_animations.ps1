# Import the retargeted Quaternius clips (tools/blender/retarget_ual.py) into the project
# (see tools/ue/import_animations.py).
#
#   powershell -File tools/ue/import_animations.ps1
#
# Close the editor first (assets are written by a separate headless editor session).
param(
    [string]$Clips = "",
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
$log = Join-Path $repo "game\MeridianRemastered\Saved\Logs\import_animations.log"

$env:MR_ANIM_DIR = if ($Clips) { (Resolve-Path $Clips).Path } else { "" }
& $Engine $proj "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'import_animations.py')" -unattended -nosplash -RenderOffscreen "-abslog=$log" | Out-Null

Select-String -Path $log -Pattern "\[import_animations\]" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
if (Select-String -Path $log -Pattern "import_animations\] FAILED" -Quiet) { exit 1 }
