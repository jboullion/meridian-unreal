#include "Core/MRSettings.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "MRSettings"

namespace
{
	const TCHAR* CVarSection = TEXT("MeridianRemastered.CVars");
	const TCHAR* KeySection = TEXT("MeridianRemastered.Keys");
	int32 KeyVersion = 1;
}

// ------------------------------------------------------------------------------ console variables

const TArray<const TCHAR*>& MRSettings::SavedCVars()
{
	static const TArray<const TCHAR*> Names = {
		TEXT("mr.Audio.Music"), TEXT("mr.Audio.MusicVolume"), TEXT("mr.Audio.Sound"), TEXT("mr.Audio.SoundVolume"),
		TEXT("mr.Audio.Loops"), TEXT("mr.Audio.Random"), TEXT("mr.UI.DamageNumbers"), TEXT("mr.Camera.FOV"),
		TEXT("mr.Chat.OriginalTyping"),
	};
	return Names;
}

void MRSettings::ApplySaved()
{
	if (!GConfig)
	{
		return;
	}
	for (const TCHAR* Name : SavedCVars())
	{
		FString Value;
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		if (V && GConfig->GetString(CVarSection, Name, Value, GGameUserSettingsIni))
		{
			V->Set(*Value, ECVF_SetByGameSetting);
		}
	}
}

void MRSettings::SetCVar(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		V->Set(Value, ECVF_SetByGameSetting);
	}
	if (GConfig)
	{
		GConfig->SetString(CVarSection, Name, *FString::SanitizeFloat(Value), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

float MRSettings::GetCVar(const TCHAR* Name, float Default)
{
	const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
	return V ? V->GetFloat() : Default;
}

// ------------------------------------------------------------------------------ keys

const TArray<FMRKeyBinding>& MRKeys::All()
{
	// the original's keys: merintr.c interface_key_table (arrows walk and turn, Ctrl attacks the
	// closest, Space goes, [ ] \ target); the rest keep ours
	static const TArray<FMRKeyBinding> Bindings = {
		{TEXT("MoveForward"), LOCTEXT("MoveForward", "Forward"), EKeys::W, EKeys::Up},
		{TEXT("MoveBack"), LOCTEXT("MoveBack", "Back"), EKeys::S, EKeys::Down},
		{TEXT("StrafeLeft"), LOCTEXT("StrafeLeft", "Step left"), EKeys::A, EKeys::A},
		{TEXT("StrafeRight"), LOCTEXT("StrafeRight", "Step right"), EKeys::D, EKeys::D},
		{TEXT("TurnLeft"), LOCTEXT("TurnLeft", "Turn left"), EKeys::Invalid, EKeys::Left},
		{TEXT("TurnRight"), LOCTEXT("TurnRight", "Turn right"), EKeys::Invalid, EKeys::Right},
		{TEXT("Walk"), LOCTEXT("Walk", "Walk (hold)"), EKeys::LeftShift, EKeys::LeftShift},
		{TEXT("Go"), LOCTEXT("Go", "Go through a door"), EKeys::SpaceBar, EKeys::SpaceBar},
		{TEXT("Attack"), LOCTEXT("Attack", "Attack"), EKeys::LeftMouseButton, EKeys::LeftControl},
		{TEXT("Look"), LOCTEXT("Look", "Look at"), EKeys::RightMouseButton, EKeys::RightMouseButton},
		{TEXT("View"), LOCTEXT("View", "Change the view"), EKeys::V, EKeys::End},
		{TEXT("Inventory"), LOCTEXT("Inventory", "Inventory"), EKeys::E, EKeys::I},
		{TEXT("Chat"), LOCTEXT("Chat", "Chat"), EKeys::Enter, EKeys::Enter},
		{TEXT("Get"), LOCTEXT("Get", "Pick up"), EKeys::G, EKeys::G},
		{TEXT("Use"), LOCTEXT("Use", "Use or open"), EKeys::F, EKeys::F},
		{TEXT("Rest"), LOCTEXT("Rest", "Rest or stand"), EKeys::R, EKeys::R},
		{TEXT("Apply"), LOCTEXT("Apply", "Use an item on..."), EKeys::U, EKeys::U},
		{TEXT("TargetNext"), LOCTEXT("TargetNext", "Next target"), EKeys::Tab, EKeys::RightBracket},
		{TEXT("TargetPrevious"), LOCTEXT("TargetPrevious", "Previous target"), EKeys::LeftBracket, EKeys::LeftBracket},
		{TEXT("TargetSelf"), LOCTEXT("TargetSelf", "Target yourself"), EKeys::Backslash, EKeys::Backslash},
		{TEXT("TargetAim"), LOCTEXT("TargetAim", "Target what you aim at"), EKeys::T, EKeys::T},
		{TEXT("Who"), LOCTEXT("Who", "Who is on"), EKeys::O, EKeys::O},
		{TEXT("Mail"), LOCTEXT("Mail", "Mail"), EKeys::L, EKeys::L},
		{TEXT("Guild"), LOCTEXT("Guild", "Guild"), EKeys::Y, EKeys::Y},
		{TEXT("Map"), LOCTEXT("Map", "The large map"), EKeys::M, EKeys::M},
		{TEXT("MapZoomIn"), LOCTEXT("MapZoomIn", "Map closer"), EKeys::Equals, EKeys::Add},
		{TEXT("MapZoomOut"), LOCTEXT("MapZoomOut", "Map farther"), EKeys::Hyphen, EKeys::Subtract},
	};
	return Bindings;
}

FKey MRKeys::Get(FName Id)
{
	FString Saved;
	if (GConfig && GConfig->GetString(KeySection, *Id.ToString(), Saved, GGameUserSettingsIni))
	{
		const FKey K(*Saved);
		if (K.IsValid())
		{
			return K;
		}
	}
	const FMRKeyBinding* B = All().FindByPredicate([Id](const FMRKeyBinding& E) { return E.Id == Id; });
	return B ? B->Modern : EKeys::Invalid;
}

void MRKeys::Set(FName Id, const FKey& Key)
{
	if (GConfig)
	{
		GConfig->SetString(KeySection, *Id.ToString(), *Key.ToString(), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	++KeyVersion;
}

void MRKeys::UsePreset(bool bOriginal)
{
	for (const FMRKeyBinding& B : All())
	{
		if (GConfig)
		{
			GConfig->SetString(KeySection, *B.Id.ToString(), *(bOriginal ? B.Original : B.Modern).ToString(), GGameUserSettingsIni);
		}
	}
	if (GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	++KeyVersion;
}

int32 MRKeys::Version()
{
	return KeyVersion;
}

FName MRKeys::UsedBy(const FKey& Key, FName Except)
{
	for (const FMRKeyBinding& B : All())
	{
		if (B.Id != Except && Get(B.Id) == Key)
		{
			return B.Id;
		}
	}
	return NAME_None;
}

#undef LOCTEXT_NAMESPACE
