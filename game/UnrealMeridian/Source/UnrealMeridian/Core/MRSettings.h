#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * The player's own settings (docs/adr/0009-user-interface.md, "Options"), kept in this computer's
 * GameUserSettings.ini (the engine's graphics settings live there too, in UGameUserSettings):
 * - the game's console variables the Options dialog sets (sound, camera, chat...), section [UnrealMeridian.CVars];
 * - the key bindings, section [UnrealMeridian.Keys], with the Modern and Original presets.
 */
namespace MRSettings
{
	/** The console variables the Options dialog changes and this file keeps. */
	UNREALMERIDIAN_API const TArray<const TCHAR*>& SavedCVars();
	/** Set the saved values (at start-up). */
	UNREALMERIDIAN_API void ApplySaved();
	/** Set a console variable now and keep it. */
	UNREALMERIDIAN_API void SetCVar(const TCHAR* Name, float Value);
	UNREALMERIDIAN_API float GetCVar(const TCHAR* Name, float Default = 0.f);
}

/** A key the player can rebind. */
struct FMRKeyBinding
{
	FName Id;
	FText Label;
	/** This game's default (WASD and the mouse) and the original client's (arrows, Ctrl attacks). */
	FKey Modern;
	FKey Original;
};

namespace MRKeys
{
	/** Every rebindable key, in the order the Options dialog lists them. */
	UNREALMERIDIAN_API const TArray<FMRKeyBinding>& All();
	/** The key bound now (the saved one, else the Modern default). */
	UNREALMERIDIAN_API FKey Get(FName Id);
	UNREALMERIDIAN_API void Set(FName Id, const FKey& Key);
	/** Every binding to a preset's keys. */
	UNREALMERIDIAN_API void UsePreset(bool bOriginal);
	/** Bumped on every change: input contexts map their keys again when it moves. */
	UNREALMERIDIAN_API int32 Version();
	/** The binding a key is used by already (none: NAME_None). */
	UNREALMERIDIAN_API FName UsedBy(const FKey& Key, FName Except = NAME_None);
}
