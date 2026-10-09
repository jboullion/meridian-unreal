# Sprite player visual check (docs/sprites.md): the -MRScreenshots tour, or the
# -MRProfile crowd, in Raza. Screenshots land in game/UnrealMeridian/Saved/Screenshots/MRTour
# (tour) or .../MRProfile (crowd); copies go to build/sprites/tour/<label>/.
#
#   powershell -File tools/sprites/run_sprite_tour.ps1 -Label uvfix
#   powershell -File tools/sprites/run_sprite_tour.ps1 -Label crowd -Crowd
#   powershell -File tools/sprites/run_sprite_tour.ps1 -Label night -Hour 22
#   powershell -File tools/sprites/run_sprite_tour.ps1 -Label unlit -Cvars "mr.Sprite.Unlit 1"
param(
    [string]$Label = "tour",
    [switch]$Crowd,
    [switch]$Monsters,      # the monster line-up (-MRMonsters)
    [string]$Look = "",
    [string]$Extra = "",
    [string]$Cvars = "",    # extra console commands, comma-separated (e.g. "mr.Sprite.Unlit 1")
    [float]$Hour = 14,      # game hour to pin (the clock follows real time otherwise); -1 = real clock
    [int]$Zone = 300,       # start zone (301 = the Inn, for indoor lighting)
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projDir = Join-Path $repo "game\UnrealMeridian"
$proj = Join-Path $projDir "UnrealMeridian.uproject"
$mode = if ($Crowd) { "-MRProfile" } elseif ($Monsters) { "-MRMonsters" } else { "-MRScreenshots" }
$shots = Join-Path $projDir ("Saved\Screenshots\" + $(if ($Crowd) { "MRProfile" } elseif ($Monsters) { "MRMonsters" } else { "MRTour" }))
if (Test-Path $shots) { Remove-Item -Recurse -Force $shots }
$args = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=1280 -resy=720 -MRStartZone=$Zone $mode"
if ($Look) { $args += " -MRSpriteLook=$Look" }
$exec = @()
if ($Hour -ge 0) { $exec += "mr.GameHour $Hour,mr.Season 1" }
if ($Cvars) { $exec += $Cvars }
if ($exec) { $args += " -ExecCmds=`"" + ($exec -join ",") + "`"" }
if ($Extra) { $args += " $Extra" }
$log = Join-Path $projDir "Saved\Logs\UnrealMeridian.log"
# a Nanite / VSM GPU page fault on the first frames sometimes kills a -game run (see run_lookdev.ps1): retry once
for ($try = 0; $try -lt 2; $try++) {
    if (Test-Path $log) { Remove-Item $log -ErrorAction SilentlyContinue }
    $p = Start-Process -FilePath $Engine -ArgumentList $args -PassThru
    $crashed = $false
    while (-not $p.WaitForExit(5000)) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern "GPU Crashed|PageFault at VA" -Quiet)) { $crashed = $true; Start-Sleep 3; $p.Kill(); break }
        if (((Get-Date) - $p.StartTime).TotalSeconds -gt 900) { $p.Kill(); Write-Host "TIMEOUT"; break }
    }
    if (-not $crashed) { break }
    Write-Host "GPU crash on start, retrying"
}
$out = Join-Path $repo "build\sprites\tour\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$shots\*.png" $out
Select-String -Path $log -Pattern "MRProfile:|MRMonsters:|Monsters:|appearance:|Sprite data|Error.*Sprite|Warning.*[Ss]prite|Monster class" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -Last 20
Write-Host "-> $out"
