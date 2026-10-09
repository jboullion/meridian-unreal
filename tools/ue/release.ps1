# Releases the game: packages it, zips it, and publishes the zips as a GitHub release of this repo,
# like Meridian Shards' `npm run release` (its tools/deploy/release.ts). Shards builds on GitHub's
# runners; we can't (no Unreal Engine there, and Content/Generated/ isn't in git), so everything
# runs here: git pushes the version and its tag, and the zip goes up to GitHub's release page.
#
#   npm run release -- 0.2.0            package Win64, bump to 0.2.0, commit, tag, push, then the upload
#   npm run release -- 0.2.0 -Linux     also the Linux build (cross-compiled; untested on real Linux)
#   npm run release -- 0.2.0 -Draft     (with gh) leave the release an unpublished draft
#   npm run release:local               zip the working tree as it is: no version, git or GitHub
#
# What ships is what's built on this machine: run build_world.ps1 first if zones or art changed.
# The version lives in DefaultGame.ini (ProjectVersion; the login screen shows it).
# The upload: with the GitHub CLI signed in (gh auth login) the script makes and publishes the release.
# Without it, git does the rest and the script opens GitHub's new-release page for the pushed tag and
# shows the zip in Explorer: drag it in, then Publish (git alone can't make a release).
# The repo is public, so is the release.
# Online play needs app://unreal-meridian in the Shards VM's GATEWAY_ORIGINS (ADR 0010).
# Output: build/release/Unreal-Meridian-<version>-<Platform>.zip
param(
	[Parameter(Position = 0)] [string]$Version,
	[switch]$Linux,
	[switch]$Draft,
	[switch]$Local,
	[switch]$SkipPackage,   # zip build/package/<Platform> as it is (already packaged for this version)
	[string]$EngineRoot = "G:\Unreal Engine\UE_5.8",
	[string]$Config = "Shipping"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$gameIni = Join-Path $repo "game\UnrealMeridian\Config\DefaultGame.ini"
$ghRepo = "jboullion/meridian-unreal"
$releaseDir = Join-Path $repo "build\release"
$tar = Join-Path $env:SystemRoot "System32\tar.exe"   # Windows' bsdtar writes zips; Git's GNU tar doesn't
$platforms = @("Win64") + $(if ($Linux) { @("Linux") } else { @() })

function Fail([string]$Message) { Write-Host "!! $Message" -ForegroundColor Red; exit 1 }

function Invoke-Git {
	$out = & git.exe -C $repo @args
	if ($LASTEXITCODE -ne 0) { Fail "git $args failed" }
	return $out
}

# Runs a native command without its stderr; PowerShell 5.1 turns redirected stderr into errors under Stop
function Get-ExitCode([scriptblock]$Block) {
	$eap = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	try { & $Block 2>$null | Out-Null } finally { $ErrorActionPreference = $eap }
	return $LASTEXITCODE
}

function Get-ProjectVersion {
	$m = [regex]::Match([IO.File]::ReadAllText($gameIni), "(?m)^ProjectVersion=(\S+)")
	if (-not $m.Success) { Fail "no ProjectVersion in $gameIni" }
	return $m.Groups[1].Value
}

function Set-ProjectVersion([string]$To) {
	$text = [IO.File]::ReadAllText($gameIni)
	[IO.File]::WriteAllText($gameIni, [regex]::Replace($text, "(?m)^ProjectVersion=\S+", "ProjectVersion=$To"))
}

# These throw rather than Fail, so a failure puts the version back (the try below)
# The archive folder holds the game directly, or (some engine versions) under the platform's cooked name
function Get-PackageDir([string]$Platform) {
	$dir = Join-Path $repo "build\package\$Platform"
	foreach ($sub in @("Windows", "Linux")) {
		if (Test-Path (Join-Path $dir "$sub\UnrealMeridian")) { return Join-Path $dir $sub }
	}
	if (-not (Test-Path (Join-Path $dir "UnrealMeridian"))) { throw "no packaged game in $dir" }
	return $dir
}

# A copy of the package with what a player needs beside it, zipped into one top-level folder
function New-ReleaseZip([string]$Platform, [string]$Label) {
	$name = "Unreal-Meridian-$Label-$Platform"
	$stage = Join-Path $releaseDir "stage\$name"
	$zip = Join-Path $releaseDir "$name.zip"
	if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
	New-Item -ItemType Directory -Force $stage | Out-Null
	# debug symbols and the stage manifests stay here (build/package/) for crash reports
	robocopy (Get-PackageDir $Platform) $stage /MIR /XF *.pdb *.debug *.sym Manifest_*.txt /NFL /NDL /NJH /NJS /NP | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "copying the $Platform package failed ($LASTEXITCODE)" }

	# Players can't reach our local dev server: offer only the ones on the internet
	$serversPath = Join-Path $stage "UnrealMeridian\Data\net\servers.json"
	if (-not (Test-Path $serversPath)) { throw "the package has no Data\net\servers.json (package.ps1 copies data/)" }
	$servers = Get-Content $serversPath -Raw | ConvertFrom-Json
	$servers.servers = @($servers.servers | Where-Object { $_.ws -notmatch "://(localhost|127\.0\.0\.1|\[::1\])[:/]" })
	[IO.File]::WriteAllText($serversPath, ($servers | ConvertTo-Json -Depth 6))

	Copy-Item (Join-Path $repo "LICENSE") (Join-Path $stage "LICENSE.txt")
	Copy-Item (Join-Path $repo "LICENSE-EXCEPTION.md") (Join-Path $stage "LICENSE-EXCEPTION.md")
	if ($Platform -eq "Win64") {
		[IO.File]::WriteAllText((Join-Path $stage "Play Offline.bat"), "@start `"`" `"%~dp0UnrealMeridian.exe`" -MROffline`r`n")
		$run = "Run UnrealMeridian.exe for the login screen, or `"Play Offline.bat`" to walk around without a server."
	} else {
		[IO.File]::WriteAllText((Join-Path $stage "play-offline.sh"), "#!/bin/sh`ncd `"`$(dirname `"`$0`")`" && exec ./UnrealMeridian.sh -MROffline `"`$@`"`n")
		$run = "First: chmod +x UnrealMeridian.sh play-offline.sh UnrealMeridian/Binaries/Linux/*`nRun ./UnrealMeridian.sh for the login screen, or ./play-offline.sh to walk around without a server.`nNeeds a Vulkan 1.3 GPU (SM6). Not yet tested on real Linux hardware."
	}
	$readme = @"
Unreal Meridian $Label ($Platform)

A free fan remaster of Meridian 59 in Unreal Engine 5: the town of Raza and the zones around it.

$run
Starting can take a minute or two (a black window while it loads).

New name and password on the login screen = a new account on the server.
Keys: WASD + mouse, Space goes through doors, E or I inventory, Enter chats, Esc or F10 the menu.
Settings, saves and the server's downloaded files: %LOCALAPPDATA%\UnrealMeridian (Windows).

Licensed under the GNU GPL version 2 with an Unreal Engine linking exception (LICENSE.txt,
LICENSE-EXCEPTION.md). Source: https://github.com/$ghRepo
"@
	[IO.File]::WriteAllText((Join-Path $stage "README.txt"), ($readme -replace "`r?`n", "`r`n"))

	if (Test-Path $zip) { Remove-Item -Force $zip }
	Write-Host "zipping $name.zip"
	& $tar -a -cf $zip -C (Split-Path -Parent $stage) $name
	if ($LASTEXITCODE -ne 0) { throw "zipping $name failed" }
	Remove-Item -Recurse -Force $stage
	$size = (Get-Item $zip).Length
	# GitHub refuses release files of 2 GiB or more
	if ($size -ge 2GB) { throw "$name.zip is $([math]::Round($size / 1GB, 2)) GB; GitHub's limit is 2 GB per file" }
	Write-Host ("  {0} ({1:N0} MB)" -f $zip, ($size / 1MB))
	return $zip
}

# ------------------------------------------------------------------------------ checks

$current = Get-ProjectVersion
if ($Local) {
	$sha = (Invoke-Git rev-parse --short HEAD)
	$dirty = if (Invoke-Git status --porcelain) { "-modified" } else { "" }
	$label = "$current-dev-$sha$dirty"
} else {
	if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "usage: npm run release -- <x.y.z> [-Linux] [-Draft] | npm run release:local" }
	# the current version itself may be released once (its tag, checked below, doesn't exist yet)
	if ([version]$Version -lt [version]$current) { Fail "$Version is older than the current version $current" }
	$tag = "v$Version"
	$label = $Version
	if ((Invoke-Git rev-parse --abbrev-ref HEAD) -ne "main") { Fail "releases are made from main" }
	if (Invoke-Git status --porcelain) { Fail "commit or stash your changes first (git status), or try npm run release:local" }
	Invoke-Git fetch --tags origin | Out-Null
	if ((Invoke-Git rev-list --count HEAD..origin/main) -ne "0") { Fail "origin/main has commits you don't: pull first" }
	if (Invoke-Git tag -l $tag) { Fail "tag $tag already exists" }
	if (Invoke-Git ls-remote --tags origin "refs/tags/$tag") { Fail "origin already has tag $tag" }
	$useGh = (Get-Command gh -ErrorAction SilentlyContinue) -and (Get-ExitCode { gh auth status }) -eq 0
	if ($useGh) {
		if ((Get-ExitCode { gh release view $tag --repo $ghRepo }) -eq 0) { Fail "GitHub already has a release for $tag; delete it or pick another version" }
	} else {
		Write-Host "the GitHub CLI isn't signed in: git pushes the tag, then you upload the zip on GitHub's page"
	}
}

# ------------------------------------------------------------------------------ package and zip

if (-not $Local -and $Version -ne $current) {
	Write-Host "version $current -> $Version"
	Set-ProjectVersion $Version
}
$zips = @()
try {
	foreach ($p in $platforms) {
		if (-not $SkipPackage) {
			Write-Host "packaging $p ($Config); the first run of a platform takes a while"
			& (Join-Path $PSScriptRoot "package.ps1") -Platform $p -Config $Config -EngineRoot $EngineRoot
		}
		$zips += New-ReleaseZip $p $label
	}
} catch {
	if (-not $Local) { Invoke-Git checkout -- $gameIni | Out-Null }
	Fail "$_"
}

if ($Local) {
	Write-Host "done: $($zips -join ', ')"
	exit 0
}

# ------------------------------------------------------------------------------ tag and publish

if (Invoke-Git status --porcelain -- $gameIni) {
	Invoke-Git add -- $gameIni | Out-Null
	Invoke-Git commit -m "Release $tag" -- $gameIni | Out-Null
}
Invoke-Git tag $tag | Out-Null
Write-Host "pushing main and $tag"
& git.exe -C $repo push origin main
if ($LASTEXITCODE -ne 0) { Fail "pushing main failed; the release commit and tag $tag are local" }
& git.exe -C $repo push origin $tag
if ($LASTEXITCODE -ne 0) { Fail "pushing $tag failed" }

if (-not $useGh) {
	$title = [uri]::EscapeDataString("Unreal Meridian $tag")
	$page = "https://github.com/$ghRepo/releases/new?tag=$tag&title=$title"
	Write-Host ""
	Write-Host "$tag is pushed. To finish the release on GitHub (the page is opening):"
	Write-Host "  1. drag in $($zips -join ', ')"
	Write-Host "  2. optionally 'Generate release notes', then 'Publish release'"
	Write-Host "  $page"
	Start-Process $page
	Start-Process explorer.exe "/select,`"$($zips[0])`""
	exit 0
}

$retry = "gh release create $tag $($zips -join ' ') --repo $ghRepo --draft --verify-tag --title `"Unreal Meridian $tag`" --generate-notes"
Write-Host "uploading to a draft release (about 1.5 GB per platform)"
& gh release create $tag @zips --repo $ghRepo --draft --verify-tag --title "Unreal Meridian $tag" --generate-notes
if ($LASTEXITCODE -ne 0) { Fail "the upload failed. $tag is pushed; to retry: $retry (or gh release upload $tag <zip> --clobber into an existing draft)" }

$names = & gh release view $tag --repo $ghRepo --json assets --jq ".assets[].name"
foreach ($z in $zips) {
	if ($names -notcontains (Split-Path -Leaf $z)) { Fail "the draft has no $(Split-Path -Leaf $z); it stays unpublished" }
}
if ($Draft) {
	Write-Host "left as a draft: https://github.com/$ghRepo/releases (publish it there)"
	exit 0
}
& gh release edit $tag --repo $ghRepo --draft=false --prerelease=false --latest | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "publishing failed; the draft is uploaded: https://github.com/$ghRepo/releases" }
Write-Host "published: https://github.com/$ghRepo/releases/tag/$tag"
