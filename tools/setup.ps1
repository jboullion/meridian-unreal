# Rebuild everything that is generated and git-ignored, in dependency order:
#   data/*.json            (committed, but regenerated here so it matches the Kod source)
#   build/textures, build/zones   (extracted reference art and blockout meshes)
#   game/.../Binaries             (compiled editor module)
#   game/.../Content/Generated    (zone meshes and the L_World level)
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup.ps1 [-SkipData] [-SkipBuild]
param(
    [string]$EngineRoot = "G:\Unreal Engine\UE_5.8",
    [switch]$SkipData,
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $repo "game\MeridianRemastered\MeridianRemastered.uproject"

function Step($name, [scriptblock]$body) {
    Write-Host "== $name"
    & $body
    if ($LASTEXITCODE -ne 0) { throw "$name failed (exit $LASTEXITCODE)" }
}

Push-Location $repo
try {
    if (-not $SkipData) {
        Step "extract Kod data" { python tools/kod_extract/extract.py }
        Step "extract zone textures" { python tools/bgf2png/bgf2png.py --textures-for-zones }
        Step "convert rooms to blockouts" { python tools/roo2gltf/roo2gltf.py --preview }
    }
    if (-not $SkipBuild) {
        Step "compile editor" {
            & (Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat") MeridianRemasteredEditor Win64 Development "-Project=$proj" -WaitMutex
        }
    }
    Step "build world level" {
        & (Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe") $proj `
            "-ExecutePythonScript=$(Join-Path $repo 'tools\ue\build_world.py')" -unattended -nosplash -RenderOffscreen | Out-Null
    }
    Write-Host "== done. Run tools/ue/run_zone_test.ps1 to verify."
}
finally {
    Pop-Location
}
