# Compile the C++ (the editor target, Development). C++ changes need this and an editor restart.
#
#   powershell -File tools/ue/compile.ps1
param(
    [string]$EngineRoot = "G:\Unreal Engine\UE_5.8",
    [string]$Config = "Development"
)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" MeridianRemasteredEditor Win64 $Config "-Project=$proj" -WaitMutex
exit $LASTEXITCODE
