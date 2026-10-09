# Package the game for a platform: cook, build, stage and zip (docs/adr/0011-linux-and-mac-builds.md).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/ue/package.ps1 -Platform Linux
# Platforms: Win64, Linux (cross-compiled from Windows; needs the Linux download component and the
# clang toolchain, see the ADR). Mac cannot be built here: use tools/ue/package_mac.sh on a Mac.
# Output: build/package/<Platform>/. Needs the world built first (tools/ue/build_world.ps1).
param(
	[ValidateSet("Win64", "Linux")] [string]$Platform = "Win64",
	[string]$EngineRoot = "G:\Unreal Engine\UE_5.8",
	[string]$Config = "Shipping"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$project = Join-Path $repo "game\UnrealMeridian\UnrealMeridian.uproject"
$out = Join-Path $repo "build\package\$Platform"

if ($Platform -eq "Linux") {
	if (-not $env:LINUX_MULTIARCH_ROOT) { $env:LINUX_MULTIARCH_ROOT = "C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\" }
	if (-not (Test-Path $env:LINUX_MULTIARCH_ROOT)) { throw "Linux clang toolchain not found at $($env:LINUX_MULTIARCH_ROOT) (see the ADR)" }
}

# A packaged game reads its JSON from <project>/Data (UMRZoneSubsystem::GetDataDir); in development it
# reads the repo's data/. Copy it in so the stage step (DirectoriesToAlwaysStageAsNonUFS) picks it up.
$dataOut = Join-Path $repo "game\UnrealMeridian\Data"
robocopy (Join-Path $repo "data") $dataOut /MIR /XD aigen /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying data/ failed ($LASTEXITCODE)" }

# The archive step only adds files, so an earlier package's leftovers (the pre-rename MeridianRemastered
# folder, say) would ship with this one
if (Test-Path $out) { Remove-Item -Recurse -Force $out }

& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
	"-project=$project" "-platform=$Platform" "-clientconfig=$Config" -noP4 `
	-build -cook -stage -pak -archive "-archivedirectory=$out" -utf8output `
	-map=/Game/Generated/Maps/L_World
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }
Write-Host "Packaged to $out"
