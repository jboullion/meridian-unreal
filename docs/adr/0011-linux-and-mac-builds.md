# ADR 0011: Linux and Mac builds

- Status: Accepted (Linux packages; nothing tested on real Linux or Mac hardware yet)
- Date: 2026-10-08

## Context
The Electron port (Meridian Shards) runs everywhere. The original client is Windows-only (Win32/Direct3D); players on Mac and Linux use Wine or CrossOver. We want the Unreal client to ship on Windows, macOS and Linux, as separate builds and installers.

Findings from reading the project (2026-10-08):
- The C++ has no Windows-only code (no `PLATFORM_WINDOWS`, Win32 includes, `.exe` launches or drive paths). Networking is Unreal's `WebSockets` + `HTTP`, the UI is Slate, so those parts are portable.
- Packaged data: `Data/` is staged as non-UFS (`DefaultGame.ini`) and read from `ProjectDir()/Data`; saves go under `ProjectSavedDir()`, which Unreal puts in a per-user folder on each OS.
- The risk is rendering: Lumen (software), virtual shadow maps and Nanite tessellation (`r.Nanite.Tessellation`, used for displaced zone art, ADR 0003) on Metal and Vulkan.
- Tools in `tools/` (PowerShell, Blender, Python) are Windows development tools. Players never need them.

## Decision
1. **Linux is cross-compiled from this Windows machine; Mac is built on a Mac.** Unreal can't build for macOS from Windows.
2. **Both platforms target SM6 only** (Lumen and Nanite need it): `DefaultEngine.ini` sets `SF_VULKAN_SM6` for Linux and `SF_METAL_SM6` for Mac.
3. **Nanite tessellation starts off on Mac and Linux** through `Config/DefaultDeviceProfiles.ini` (`r.Nanite.Tessellation=0`). The art falls back to flat meshes with normal maps, as it does when a material has no displacement. Turn it on per profile once a real machine shows it works.
4. **Packaging scripts:** `tools/ue/package.ps1 -Platform Win64|Linux` (output in `build/package/<Platform>/`) and `tools/ue/package_mac.sh` (on the Mac).

## What was set up on the maintainer's machine (2026-10-08)
- **Linux clang cross-compile toolchain** `v26_clang-20.1.8-rockylinux8` (1.0 GB, from `https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe`, the version named in `Engine/Config/Linux/Linux_SDK.json`). Installed to `C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\`; the installer set the machine variable `LINUX_MULTIARCH_ROOT`. The installer is kept in `build/toolchains/` (git-ignored) and can be deleted.
- **Linux runtime files:** these were missing at first ( `Build.bat MeridianRemastered Linux Development` stops with "Missing files required to build Linux targets. Enable Linux as an optional download component in the Epic Games Launcher." The Epic Games Launcher isn't installed on this machine, so install it, then Library > UE 5.8 > Options > Target Platforms > Linux (several GB). Mac needs nothing here.)  *Done 2026-10-08.*

## First Linux package (2026-10-08)
The Launcher's Linux component was installed and `tools/ue/package.ps1 -Platform Linux` built, cooked and staged a Shipping build: `build/package/Linux/` (1.2 GB, `MeridianRemastered.sh`). The first full run took about 70 minutes (shaders for Vulkan SM6); later runs are incremental.
- **The Linux compiler is stricter (`-Werror`).** It rejected `for (const TPair<FString, TSharedPtr<FJsonValue>>& X : Obj->Values)` (12 places in `MRSpriteData.cpp` and `MRInventorySource.cpp`): the key is `TSharedString`, so the reference binds to a temporary. Fixed by iterating by value (`for (const TPair<...> X : ...)`). Don't change these to `const auto&`: the key type has no `Split`/`StartsWith`. Expect more of this kind when Mac (also clang) is built.
- **Packaged builds need `Data/`.** The game reads JSON from `<project>/Data` (`UMRZoneSubsystem::GetDataDir`) and only falls back to the repo's `data/` in development; nothing created that folder, so a package had no zone data. `package.ps1` now mirrors `data/` (without `aigen/`) into `game/MeridianRemastered/Data/` (git-ignored) before packaging. `package_mac.sh` still needs the same step.
- **Smoke test under WSL2 (Ubuntu, `-nullrhi -nosound -MROffline`):** the binary starts and stays up for 90 s with no crash or missing-library error. A Shipping build prints almost nothing, so this proves little beyond "it launches". It does not test rendering, zones or the network. A real test needs a Linux machine, and a Development build (`-Config Development`) if you want logs.

## First Win64 package with the online client (2026-10-08, after M9)
`tools/ue/package.ps1 -Platform Win64 -Config Development` built a package with everything since M1: `build/package/Win64/` (2.1 GB). The first run took about 15 minutes (the editor had already compiled most shaders), a rerun about 6.
- **Folders loaded by path must be cooked by name.** The cook only follows references from the map, and the sprites, UI art, sounds, materials and rain kit are loaded by path at run time. `DefaultGame.ini` lists them in `DirectoriesToAlwaysCook` (`/Game/Generated/Runtime`, `Sprites`, `UI`, `Audio`, `Environment/Materials`, `Environment/Kit/SM_Precip`). The first package without them had no sprites, icons, minimap or runtime-room material.
- **The packaged game passes the online test:** run with the same flags as `run_net_test.ps1` (`MeridianRemastered.exe /Game/Generated/Maps/L_World -MRNetTest -MRServer=Local ...`), it reports `DONE 58/58` headless (`-nullrhi`) and in a window (`build/package/pkg_nettest*.log`, pictures in `build/package/win64_package_shots.png`).
- **From Git Bash, set `MSYS_NO_PATHCONV=1`.** Otherwise Bash turns `/Game/Generated/Maps/L_World` into `C:/Program Files/Git/Game/...` and the game starts on the wrong map.

## To do when a machine is available
Linux (real machine, or a Steam Deck in desktop mode):
1. Finish the Launcher step above, then run `tools/ue/package.ps1 -Platform Linux`.
2. Copy `build/package/Linux/` over, `chmod +x` the game binary, run it. Needs a Vulkan 1.3 GPU with a recent driver (SM6).
3. Check: the login screen, the world rendering (Lumen, shadows, grass, fire), audio, `-MRNetTest` (`MRNetTest: DONE 5/5` against a reachable server), the Ctrl+wheel zoom.
4. Try `r.Nanite.Tessellation=1` in the console; if the zone art looks right, remove the Linux override.

Mac (Apple Silicon preferred):
1. Install Xcode and UE 5.8.x, copy the repo with its generated content, run `tools/ue/package_mac.sh`.
2. Same checks as Linux. Ctrl is awkward on Mac: consider Cmd for the zoom.
3. Distribution needs an Apple Developer account for signing and notarization, otherwise players must bypass Gatekeeper.

Expect a scalability preset (shadows, Lumen quality) to be needed for integrated GPUs on both.

## Testing without the hardware
- **Linux, partly.** WSL2 Ubuntu is installed here (WSLg gives Linux windows and a Direct3D-backed Vulkan driver). It can launch the Linux build to prove the binary starts, finds its files and reaches the login screen, and `-nullrhi` runs would check logic. It will not be reliable for SM6, Lumen or Nanite, so it can't judge rendering or performance. A Hyper-V or VirtualBox Linux VM has no usable GPU, so it is worse. The reliable options are a live USB or dual boot on this PC (the RTX 3070 runs Linux with NVIDIA drivers) or a Steam Deck.
- **Mac, no.** macOS can't legally run on non-Apple hardware (Apple's licence), and VMs have no Metal GPU anyway. Options: a friend's Mac, or a rented cloud Mac (AWS EC2 Mac instances, MacStadium; hourly or daily rental, remote desktop). A cloud Mac is enough to build, notarize and smoke test, but GPU frame rates will be limited.
- **Windows-side checks that help:** force the same quality level (console `sg.*`, `r.Nanite.Tessellation=0`) to see how the game looks with the Mac/Linux profile; run `tools/ue/run_lookdev.ps1` with that cvar.

## Android (parked: think about it much later)
Assessed 2026-10-08, not started. It might be an option someday; it is not planned.
- **It would take two big jobs, not one.** (1) A mobile rendering path. Android uses the mobile renderer on Vulkan, where Lumen and virtual shadow maps don't run and Nanite support is limited and device-dependent (UE 5.8's current state is unchecked). The look (Lumen, Nanite displacement, SM6 requirement, moods in ADR 0005, fire lights) would need a cheaper lighting and material path, plus smaller textures and Play Asset Delivery for the store size limits. Rough guess: weeks to a couple of months. (2) A touch interface. The Slate UI is mouse and keyboard only (WASD + mouse look, number-key hotbar, numpad spells, Enter chat, Minecraft-style inventory clicks). It needs a virtual joystick, camera drag, tap-to-interact and a "go" button, on-screen hotbar, spell and chat buttons, a touch inventory design, DPI scaling, safe areas, bigger targets and soft-keyboard text entry. Slate supports touch and the widgets are shared, so this is new controls and layouts, not a rewrite. Rough guess: a few weeks for a usable first pass.
- **If we ever do it:** treat it as high-end phones only, and start with a spike (package, reach the login screen, load one zone on a real device) to measure the rendering gap before any UI work.
- **The cheaper route to phones is the Electron/browser port** (Meridian Shards): a touch layout for the web client, shipped as a PWA or with Capacitor. That is on its TODO.md as something we do want to do eventually.
