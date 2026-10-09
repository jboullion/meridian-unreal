#!/bin/bash
# Package the game for macOS. Run on a Mac with Xcode and the same Unreal version (5.8.x) installed
# (docs/adr/0011-linux-and-mac-builds.md). The repo needs its generated content (Content/Generated)
# and data/ already built on Windows, or built with the tools here on the Mac.
#   tools/ue/package_mac.sh [EngineRoot] [Config]
set -euo pipefail
ENGINE="${1:-/Users/Shared/Epic Games/UE_5.8}"
CONFIG="${2:-Shipping}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$REPO/build/package/Mac"

# A packaged game reads its JSON from <project>/Data; stage a copy of data/ (see package.ps1)
mkdir -p "$REPO/game/UnrealMeridian/Data"
rsync -a --delete --exclude aigen/ "$REPO/data/" "$REPO/game/UnrealMeridian/Data/"

"$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
	-project="$REPO/game/UnrealMeridian/UnrealMeridian.uproject" \
	-platform=Mac -clientconfig="$CONFIG" -noP4 \
	-build -cook -stage -pak -archive -archivedirectory="$OUT" -utf8output \
	-map=/Game/Generated/Maps/L_World
echo "Packaged to $OUT (still needs signing and notarization before others can open it)"
