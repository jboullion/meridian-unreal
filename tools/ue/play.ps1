# Start the game in a window, or open the editor.
#
#   powershell -File tools/ue/play.ps1                  (offline, at the Raza Inn)
#   powershell -File tools/ue/play.ps1 -Online          (the login screen: the servers in data/net/servers.json)
#   powershell -File tools/ue/play.ps1 -StartZone 306   (offline, in another zone; 306 the Crypt)
#   powershell -File tools/ue/play.ps1 -Editor          (open the project in the editor)
param(
    [switch]$Online,
    [switch]$Editor,
    [int]$StartZone = 0,
    [double]$GameHour = -1,
    [switch]$Fullscreen,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [string]$Engine = "G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
if ($Editor) {
    Start-Process -FilePath $Engine -ArgumentList "`"$proj`""
    exit 0
}
$gameArgs = "`"$proj`" /Game/Generated/Maps/L_World -game"
$gameArgs += $(if ($Fullscreen) { " -fullscreen" } else { " -windowed -resx=$ResX -resy=$ResY" })
if (-not $Online) { $gameArgs += " -MROffline" }
if ($StartZone -gt 0) { $gameArgs += " -MRStartZone=$StartZone" }
if ($GameHour -ge 0) { $gameArgs += " -MRGameHour=$GameHour" }
Start-Process -FilePath $Engine -ArgumentList $gameArgs
