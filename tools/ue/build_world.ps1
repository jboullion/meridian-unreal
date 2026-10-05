# Build /Game/Generated (zone levels, environment art, L_World) with tools/ue/build_world.py.
# Incremental: only what changed since the last build is rebuilt (tools/ue/build_cache.py).
#
#   powershell -File tools/ue/build_world.ps1                         # in the open editor, else headless
#   powershell -File tools/ue/build_world.ps1 -Clean                  # delete everything generated first
#   powershell -File tools/ue/build_world.ps1 -Headless               # never use the open editor
#   powershell -File tools/ue/build_world.ps1 -Script zone_mood.py    # lighting only
#   $env:MR_MOOD = "raza_afternoon"; powershell -File tools/ue/build_world.ps1 -Script zone_mood.py
#
# With the editor open on the project, the script runs inside it (Python remote execution,
# tools/ue/run_in_editor.py): no editor startup, and the viewport shows the result. Otherwise a
# headless editor starts on the engine's empty Entry map: opening L_World at startup renders the whole
# Nanite-tessellated town in the first frames of a headless editor, which has crashed the GPU
# driver (D3D12 page fault) on an RTX 3070.
param(
    [string]$Script = "build_world.py",
    [switch]$Clean,
    [switch]$Headless,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projDir = Join-Path $repo "game\MeridianRemastered"
$proj = Join-Path $projDir "MeridianRemastered.uproject"
$py = Join-Path $repo "tools\ue\$Script"
$log = Join-Path $projDir "Saved\Logs\MeridianRemastered.log"
$started = Get-Date

if (-not $Headless) {
    $remoteArgs = @("$repo\tools\ue\run_in_editor.py", "--engine", $Engine, $Script)
    if ($Clean) { $remoteArgs += "--clean" }
    python @remoteArgs
    if ($LASTEXITCODE -ne 3) { exit $LASTEXITCODE }
    Write-Host "starting a headless editor"
}

if ($Clean -and $Script -eq "build_world.py") {
    # nothing has these loaded yet, so deleting the files is instant (the editor's own delete gathers
    # references for every asset: minutes)
    foreach ($dir in @("Content\Generated\Maps", "Content\Generated\Zones", "Content\Generated\Environment", "Saved\MRBuild")) {
        $path = Join-Path $projDir $dir
        if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    }
}

$p = Start-Process -FilePath $Engine -PassThru -Wait -NoNewWindow -RedirectStandardOutput "NUL" -ArgumentList `
    "`"$proj`" /Engine/Maps/Entry -ExecutePythonScript=`"$py`" -unattended -nosplash -RenderOffscreen"

$lines = Select-String -Path $log -Pattern "\[build_world\]|\[zone_mood\]|\[environment_materials\]|LogPython: Error|GPU Crashed" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
$lines | Select-Object -Last 25
if ($lines -match "LogPython: Error|GPU Crashed") { Write-Host "FAIL (log: $log)"; exit 1 }
Write-Host ("OK in {0:N0} s (editor startup included)" -f ((Get-Date) - $started).TotalSeconds)
