# In-engine comparison of relief maps (tools/textures/ai_maps.py). For each displacement range and
# method: copy the method's T_<grd>_H/_N.png over the generated placeholders, rebuild the world (only
# those textures re-import), capture the building close-ups as look-dev label
# ai_<method>_<range>cm[_plain]. Afterwards the make_placeholders.py maps, the default range and (with
# -Plain) the rebuilt facades come back.
#
#   powershell -File tools/lookdev/ai_maps_test.ps1 -Plain -Ranges 5,15 -Methods current,marigold,dav2,combo
#
# -Plain builds the buildings from the original blockout geometry only (build_zone_art.py --plain:
# no window cuts, trims or cut-out solids), so the height maps are the only relief.
param(
    [string[]]$Methods = @("current", "marigold", "dav2", "combo"),
    [string[]]$Ranges = @("5"),
    [switch]$Plain,
    [string[]]$Cameras = @("hall_corner_close", "inn_close", "shops_side", "tavern_door", "temple_gable",
                           "hut_door", "west_street", "north_gate", "shops_row", "inn_front"),
    [string]$Blender = "H:\Steam\steamapps\common\Blender\blender.exe"
)
$ErrorActionPreference = "Stop"
$Methods = $Methods -split ","   # "a,b" through powershell -File arrives as one string
$Ranges = $Ranges -split ","
$Cameras = $Cameras -split ","
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$placeholders = Join-Path $repo "build\textures_placeholder"
$current = Join-Path $repo "build\texai\out\current"
$lookdevArgs = if ($Cameras -contains "all") { @() } else { @("-Only", ($Cameras -join ",")) }  # "all": every bookmark

function Zone-Art([string[]]$extra) {
    # a separate process with its output in a file: Windows PowerShell turns Blender's stderr
    # warnings into terminating errors under $ErrorActionPreference = "Stop"
    $log = Join-Path $repo "build\texai\zone_art.log"
    $script = Join-Path $repo "tools\blender\build_zone_art.py"
    $p = Start-Process -FilePath $Blender -Wait -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
        -ArgumentList (@("-b", "--factory-startup", "-P", "`"$script`"", "--", "--rid", "300") + $extra)
    Select-String -Path $log -Pattern "\] openings:|Traceback" | ForEach-Object { $_.Line }
    if ($p.ExitCode -ne 0 -or (Select-String -Path $log -Pattern "Traceback" -Quiet)) { throw "build_zone_art failed (log: $log)" }
}

function Build-World {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\build_world.ps1") | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "world build failed" }
}

# the make_placeholders.py files every tested method replaces (maps, and base colours for methods
# that bring their own), to compare and to restore
New-Item -ItemType Directory -Force $current | Out-Null
foreach ($m in $Methods) {
    Get-ChildItem (Join-Path $repo "build\texai\out\$m") -Filter "T_*.png" | ForEach-Object {
        if (-not (Test-Path (Join-Path $current $_.Name))) { Copy-Item -Force (Join-Path $placeholders $_.Name) $current }
    }
}

try {
    if ($Plain) { Zone-Art @("--plain") }
    foreach ($r in $Ranges) {
        $env:MR_DISPLACEMENT_RANGE_CM = $r
        foreach ($m in $Methods) {
            $label = "ai_${m}_${r}cm" + $(if ($Plain) { "_plain" } else { "" })
            Write-Host "== $label"
            Copy-Item -Force (Join-Path $repo "build\texai\out\$m\T_*.png") $placeholders
            Build-World
            powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\run_lookdev.ps1") -Label $label @lookdevArgs | Select-Object -Last 1
        }
    }
}
finally {
    Write-Host "== restoring maps, range and facades"
    Remove-Item Env:\MR_DISPLACEMENT_RANGE_CM -ErrorAction SilentlyContinue
    Copy-Item -Force (Join-Path $current "T_*.png") $placeholders
    if ($Plain) { Zone-Art @() }
    Build-World
}
