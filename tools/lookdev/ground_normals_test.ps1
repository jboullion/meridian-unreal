# In-engine comparison of ground normal maps (docs/adr/0003). For each variant
# (tools/lookdev/ground_normals_variant.py: current, strong, stones): rewrite materials.json, remake the
# textures, rebuild the world, then capture the cameras under each mood as look-dev label
# gn_<variant>_<mood>. Afterwards materials.json, the textures and the level's mood come back.
#
#   powershell -File tools/lookdev/ground_normals_test.ps1
#   powershell -File tools/lookdev/ground_normals_test.ps1 -Variants current,stones -Moods raza_morning
param(
    [string[]]$Variants = @("current", "strong", "stones"),
    [string[]]$Moods = @("raza_afternoon", "raza_morning"),
    [string[]]$Cameras = @("ground_square", "ground_path", "north_gate", "west_street", "square_overview", "pond_fence")
)
$ErrorActionPreference = "Stop"
$Variants = $Variants -split ","   # "a,b" through powershell -File arrives as one string
$Moods = $Moods -split ","
$Cameras = $Cameras -split ","
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$materials = Join-Path $repo "data\environment\materials.json"
$backup = Join-Path $repo "build\lookdev\materials.ground_test_backup.json"
New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
Copy-Item $materials $backup -Force

function Run($file, [string[]]$arguments) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo $file) @arguments | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "$file failed" }
}

function Build-Textures-And-World {
    python (Join-Path $repo "tools\textures\make_placeholders.py") | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { throw "make_placeholders.py failed" }
    Run "tools\ue\build_world.ps1" @()
}

try {
    foreach ($v in $Variants) {
        Write-Host "== variant $v"
        python (Join-Path $repo "tools\lookdev\ground_normals_variant.py") $v $backup
        if ($LASTEXITCODE -ne 0) { throw "ground_normals_variant.py failed" }
        Build-Textures-And-World
        foreach ($m in $Moods) {
            $env:MR_MOOD = $m
            Run "tools\ue\build_world.ps1" @("-Script", "zone_mood.py")
            Run "tools\ue\run_lookdev.ps1" @("-Label", "gn_${v}_$m", "-Only", ($Cameras -join ","))
        }
        Remove-Item Env:\MR_MOOD -ErrorAction SilentlyContinue
    }
}
finally {
    Write-Host "== restoring materials.json, the textures and the level's mood"
    Copy-Item $backup $materials -Force
    Remove-Item Env:\MR_MOOD -ErrorAction SilentlyContinue
    Build-Textures-And-World
    Run "tools\ue\build_world.ps1" @("-Script", "zone_mood.py")
}
