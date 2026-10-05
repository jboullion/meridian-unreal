# In-engine comparison of base-colour upscalers (make_placeholders.py --esrgan-model). For each
# model: remake every texture with it (Marigold relief from the same images), rebuild the world, capture
# the cameras as look-dev label up_<model>. "default" is the normal ESRGAN_RULES pipeline. Afterwards
# the default textures come back (cached, so that is quick).
#
#   powershell -File tools/lookdev/upscaler_test.ps1 -Models default,4xTextureDAT2_otf,4xNomos8kDAT
#
# Models are Real-ESRGAN names or build/texai/models/<name>.safetensors (tools/textures/upscale.py).
param(
    [string[]]$Models = @("default"),
    [string[]]$Cameras = @("shops_side", "hall_corner_close", "temple_gable", "inn_front", "inn_close", "sign_smith",
                           "tavern_door", "hut_door", "west_street", "pond_fence")
)
$ErrorActionPreference = "Stop"
$Models = $Models -split ","   # "a,b" through powershell -File arrives as one string
$Cameras = $Cameras -split ","
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$lookdevArgs = if ($Cameras -contains "all") { @() } else { @("-Only", ($Cameras -join ",")) }

function Textures([string]$model) {
    $extra = if ($model -eq "default") { @() } else { @("--esrgan-model", $model) }
    $log = Join-Path $repo "build\texai\upscaler_test.log"
    $p = Start-Process -FilePath python -Wait -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
        -ArgumentList (@("`"$(Join-Path $repo 'tools\textures\make_placeholders.py')`"") + $extra)
    Select-String -Path $log -Pattern "upscaled|texture sets|relief:" | ForEach-Object { $_.Line }
    if ($p.ExitCode -ne 0) { throw "make_placeholders failed (log: $log, $log.err)" }
}

function Build-World {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\build_world.ps1") | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "world build failed" }
}

try {
    foreach ($m in $Models) {
        Write-Host "== up_$m"
        Textures $m
        Build-World
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tools\ue\run_lookdev.ps1") -Label "up_$m" @lookdevArgs | Select-Object -Last 1
    }
}
finally {
    if ($Models[-1] -ne "default") {
        Write-Host "== restoring the default textures"
        Textures "default"
        Build-World
    }
}
