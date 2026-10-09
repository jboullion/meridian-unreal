# Environment look-dev captures (docs/adr/0003): renders every bookmark in
# data/environment/lookdev_cameras.json in a standalone game (-MRLookDev), copies the images to
# build/lookdev/<Label>/ and, with -Compare, writes a side-by-side sheet against another label.
#
#   powershell -File tools/ue/run_lookdev.ps1 -Label baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label mood1 -Compare baseline
#   powershell -File tools/ue/run_lookdev.ps1 -Label hall -Only hall_south,hall_front
#   powershell -File tools/ue/run_lookdev.ps1 -Label slow -Settle 3       (fixed 3 s per camera, the old way)
#     (by default each camera is saved as soon as its image settles, with clouds and wind frozen;
#      see Source/.../Tests/MRLookDevTour.h)
#   powershell -File tools/ue/run_lookdev.ps1 -Label night -GameHour 23          (any time of day, docs/adr/0005)
#   powershell -File tools/ue/run_lookdev.ps1 -Label dusk -Mood raza_dusk        (one mood, no day/night cycle)
#   powershell -File tools/ue/run_lookdev.ps1 -Label rain -Weather storm        (a storm at full strength, docs/adr/0005)
#   powershell -File tools/ue/run_lookdev.ps1 -Label snow -Weather storm -Season 3   (winter: Raza snows)
#   powershell -File tools/ue/run_lookdev.ps1 -Label props1 -Gallery     (the prop gallery: every prop and tree beside its
#      sprite, cameras from data/environment/gallery_cameras.json; tools/environment/prop_gallery.py, docs/adr/0007)
#   powershell -File tools/ue/run_lookdev.ps1 -Label perf -Profile -ResX 1920 -ResY 1080
#     (-Profile: per camera, frame times with everything on / Nanite tessellation off / grass hidden / fire lights off,
#      plus CSV-profiler captures; summarise with python tools/lookdev/profile_report.py perf)
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Compare = "",
    # Meridian game hour (MRGameTimeSubsystem): the day/night cycle, sun and clock faces; pinned so
    # captures compare, -1 = real time. 17 puts the sun where the afternoon mood was tuned.
    [double]$GameHour = 17,
    # pin one mood from moods.json instead of the day/night cycle (UMREnvironmentSubsystem -MRMood)
    [string]$Mood = "",
    # zone the player starts in (its neighbours stream in too); Raza reaches every Raza interior
    [int]$StartZone = 300,
    # storm or clear in every weather zone, at full strength from the start (AMRGameState
    # -MRWeather); clear by default so captures compare, "roll" follows the server's daily rolls.
    # Lightning is off for stills unless -Lightning.
    [string]$Weather = "clear",
    [switch]$Lightning,
    # hold one lightning stroke on screen (bolt and flash) for a still; implies -Lightning
    [switch]$LightningHold,
    # rain, snow or sand whatever the season brings (UMREnvironmentSubsystem -MRWeatherKind)
    [string]$WeatherKind = "",
    # pin the season (0 spring, 1 summer, 2 fall, 3 winter): it decides rain or snow and tints the
    # foliage; default summer (the original's colours, so captures compare); -1 = the clock
    [int]$Season = 1,
    # console commands to run at start, e.g. "mr.Weather.Splashes 0" (comma-separated)
    [string]$Exec = "",
    [string[]]$Only = @(),
    [switch]$Profile,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [double]$Settle = 0,
    # > 0: sound on, and the main mix recorded for this many seconds at each camera (<camera>.wav,
    # with the audio system's log as audio.log): tools/audio/audio_report.py turns them into a sheet
    [double]$Audio = 0,
    # the player's character at each camera, seen through its own camera (the look-dev hides it otherwise)
    [switch]$WithPawn,
    # frames at these times (s) after each camera cut, unsettled: what a zone change looks like, e.g. "0.05,0.2,0.5,1,2,4"
    [string]$Burst = "",
    # the prop gallery (-MRGallery) and its cameras instead of the world's
    [switch]$Gallery,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$TimeoutSeconds = 900
)

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$saved = Join-Path $repo "game\UnrealMeridian\Saved\Screenshots\MRLookDev\$Label"
$out = Join-Path $repo "build\lookdev\$Label"
$log = Join-Path $repo "game\UnrealMeridian\Saved\Logs\lookdev-$Label.log"

if (Test-Path $saved) { Remove-Item -Recurse -Force $saved }
# -unattended: a GPU crash exits instead of waiting on an error dialog (then we try once more)
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game -windowed -resx=$ResX -resy=$ResY$(if ($Audio -gt 0) { '' } else { ' -nosound' }) -unattended -MRStartZone=$StartZone -MRLookDev -MRLookDevLabel=$Label -abslog=`"$log`""
if ($Gallery) { $gameArgs = $gameArgs.Replace("-MRStartZone=$StartZone", "-MRGallery -MRLookDevCameras=gallery_cameras.json") }
if ($Only) { $gameArgs += " -MRLookDevOnly=" + ($Only -join ",") }
if ($Profile) { $gameArgs += " -MRLookDevProfile" }
if ($Audio -gt 0) { $gameArgs += " -MRLookDevAudio=$Audio" }
if ($WithPawn) { $gameArgs += " -MRLookDevWithPawn" }
if ($Burst) { $gameArgs += " -MRLookDevBurst=$Burst" }
if ($Settle -gt 0) { $gameArgs += " -MRLookDevSettle=$Settle" }
if ($GameHour -ge 0) { $gameArgs += " -MRGameHour=$GameHour" }
if ($Mood) { $gameArgs += " -MRMood=$Mood" }
if ($Weather -and $Weather -ne "roll") { $gameArgs += " -MRWeather=$Weather" }
if ($LightningHold) { $gameArgs += " -MRLightningHold" }
elseif (-not $Lightning) { $gameArgs += " -MRNoLightning" }
if ($WeatherKind) { $gameArgs += " -MRWeatherKind=$WeatherKind" }
if ($Season -ge 0) { $gameArgs += " -MRSeason=$Season" }
if ($Exec) { $gameArgs += " -ExecCmds=`"$Exec`"" }

$started = Get-Date
for ($attempt = 1; $attempt -le 2; $attempt++) {
    $p = Start-Process -FilePath $Engine -PassThru -ArgumentList $gameArgs
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Write-Host "FAIL: timed out after $TimeoutSeconds s (log: $log)"
        exit 1
    }
    if (-not (Select-String -Path $log -Pattern "GPU crash detected" -Quiet)) { break }
    $dump = Join-Path (Split-Path $log) ("lookdev-$Label-gpucrash-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
    Copy-Item $log $dump
    Write-Host "GPU crash (attempt $attempt; log kept as $dump)"
    if ($attempt -eq 2) { exit 1 }
}
Get-Content $log | Select-String "MRLookDev: .*\.png \(" | ForEach-Object { $_.Line -replace '^.*[\\/]', '  ' }
Write-Host ("captured in {0:N0} s" -f ((Get-Date) - $started).TotalSeconds)

if (-not (Test-Path $saved)) { Write-Host "FAIL: no screenshots (log: $log)"; exit 1 }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -Force (Join-Path $saved "*.png") $out
if ($Profile) {
    Copy-Item -Force (Join-Path $saved "*.csv") $out -ErrorAction SilentlyContinue
    Select-String -Path $log -Pattern "MRLookDevProfile:" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } |
        Set-Content -Encoding utf8 (Join-Path $out "profile.log")
}
if ($Audio -gt 0) {
    Copy-Item -Force (Join-Path $saved "*.wav") $out -ErrorAction SilentlyContinue
    Select-String -Path $log -Pattern "MRAudio: |MRLookDev: .*\.png|MRLookDevAudio:" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } |
        Set-Content -Encoding utf8 (Join-Path $out "audio.log")
}
Get-ChildItem $out -Filter *.png | ForEach-Object { Write-Host $_.FullName }

if ($Compare) {
    python (Join-Path $repo "tools\lookdev\compare.py") $Compare $Label
}
