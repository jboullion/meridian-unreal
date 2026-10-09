#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * The player's own settings (docs/adr/0009-user-interface.md, "Options"), kept in this computer's
 * GameUserSettings.ini (the engine's graphics settings live there too, in UGameUserSettings):
 * - the game's console variables the Options dialog sets (sound, camera, chat...), section [MeridianRemastered.CVars];
 * - the key bindings, section [MeridianRemastered.Keys], with the Modern and Original presets.
 */
namespace MRSettings
{
	/** The console variables the Options dialog changes and this file keeps. */
	MERIDIANREMASTERED_API const TArray<const TCHAR*>& SavedCVars();
	/** Set the saved values (at start-up). */
	MERIDIANREMASTERED_API void ApplySaved();
	/** Set a console variable now and keep it. */
	MERIDIANREMASTERED_API void SetCVar(const TCHAR* Name, float Value);
	MERIDIANREMASTERED_API float GetCVar(const TCHAR* Name, float Default = 0.f);
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
	MERIDIANREMASTERED_API const TArray<FMRKeyBinding>& All();
	/** The key bound now (the saved one, else the Modern default). */
	MERIDIANREMASTERED_API FKey Get(FName Id);
	MERIDIANREMASTERED_API void Set(FName Id, const FKey& Key);
	/** Every binding to a preset's keys. */
	MERIDIANREMASTERED_API void UsePreset(bool bOriginal);
	/** Bumped on every change: input contexts map their keys again when it moves. */
	MERIDIANREMASTERED_API int32 Version();
	/** The binding a key is used by already (none: NAME_None). */
	MERIDIANREMASTERED_API FName UsedBy(const FKey& Key, FName Except = NAME_None);
}
