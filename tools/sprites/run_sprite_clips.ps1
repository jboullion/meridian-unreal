# Smoothing comparison (docs/sprites.md Phase 4): record the same clip with each smoothing variant
# (UMRSpriteClipTour, fixed 30 fps game step) and stitch them side by side into one GIF:
# build/sprites/clips/<clip>.gif (+ <clip>_<variant>/ frames).
#
#   powershell -File tools/sprites/run_sprite_clips.ps1 -Clip walk
#   powershell -File tools/sprites/run_sprite_clips.ps1 -Clip dance -Look test_female
#   powershell -File tools/sprites/run_sprite_clips.ps1 -Clip weapon_attack -Look test_sword -Variants original,tweens
param(
    [string]$Clip = "walk",
    [string]$Look = "test_male",
    [int]$Frames = 60,
    [string[]]$Variants = @("original", "tweens", "tweens_motion", "crossfade"),
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)
$Variants = $Variants | ForEach-Object { $_ -split "," } | Where-Object { $_ }   # "-Variants a,b" through -File arrives as one string
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projDir = Join-Path $repo "game\MeridianRemastered"
$proj = Join-Path $projDir "MeridianRemastered.uproject"
$log = Join-Path $projDir "Saved\Logs\MeridianRemastered.log"
$shots = Join-Path $projDir "Saved\Screenshots\MRClip"
$cvars = @{
    "original"      = "mr.Sprite.Smooth.Tweens 0,mr.Sprite.Smooth.Crossfade 0,mr.Sprite.Smooth.AngleFade 0,mr.Sprite.Smooth.Motion 0"
    "tweens"        = "mr.Sprite.Smooth.Tweens 1,mr.Sprite.Smooth.Crossfade 0,mr.Sprite.Smooth.Motion 0"
    "tweens_motion" = "mr.Sprite.Smooth.Tweens 1,mr.Sprite.Smooth.Crossfade 0,mr.Sprite.Smooth.Motion 1"
    "crossfade"     = "mr.Sprite.Smooth.Tweens 0,mr.Sprite.Smooth.Crossfade 0.5,mr.Sprite.Smooth.Motion 0"
    "all"           = "mr.Sprite.Smooth.Tweens 1,mr.Sprite.Smooth.Crossfade 0.4,mr.Sprite.Smooth.Motion 1"
}
# every variant: the same afternoon (the game clock follows real time) and no motion blur
$common = "mr.GameHour 14,mr.Season 1,r.MotionBlurQuality 0,"
$outRoot = Join-Path $repo "build\sprites\clips"
foreach ($v in $Variants) {
    for ($try = 0; $try -lt 2; $try++) {
        if (Test-Path $shots) { Remove-Item -Recurse -Force $shots }
        if (Test-Path $log) { Remove-Item $log -ErrorAction SilentlyContinue }
        $args = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=1280 -resy=720 -MRStartZone=300 " +
                "-MRSpriteLook=$Look -MRSpriteClip=$Clip -MRClipFrames=$Frames -UseFixedTimeStep -FPS=30 -ExecCmds=`"$common$($cvars[$v])`""
        $p = Start-Process -FilePath $Engine -ArgumentList $args -PassThru
        $crashed = $false
        while (-not $p.WaitForExit(5000)) {
            if ((Test-Path $log) -and (Select-String -Path $log -Pattern "GPU Crashed|PageFault at VA" -Quiet)) { $crashed = $true; Start-Sleep 3; $p.Kill(); break }
            if (((Get-Date) - $p.StartTime).TotalSeconds -gt 600) { $p.Kill(); Write-Host "TIMEOUT"; break }
        }
        if (-not $crashed) { break }
        Write-Host "GPU crash on start, retrying"
    }
    $dst = Join-Path $outRoot "${Clip}_$v"
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item "$shots\*.png" $dst
    Write-Host ("{0}: {1} frames" -f $v, (Get-ChildItem $dst).Count)
}
python (Join-Path $PSScriptRoot "clip_gif.py") $Clip @Variants
