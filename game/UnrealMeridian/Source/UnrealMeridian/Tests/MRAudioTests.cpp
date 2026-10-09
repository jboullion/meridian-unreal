// Automation tests for the sound data and the audio system's pure parts (docs/adr/0006).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Audio;Quit" -unattended -nullrhi

#include "Audio/MRAudioSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "Zones/MRZoneSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> ReadAudioJson(const TCHAR* Name, const TCHAR* Field)
	{
		FString Text;
		TSharedPtr<FJsonObject> Obj;
		const TSharedPtr<FJsonObject>* Inner = nullptr;
		FFileHelper::LoadFileToString(Text, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("audio"), Name));
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Obj);
		return Obj.IsValid() && Obj->TryGetObjectField(Field, Inner) ? *Inner : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAudioDataTest, "Meridian.Audio.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRAudioDataTest::RunTest(const FString& Parameters)
{
	// the original's attenuation (clientd3d audio.c): gentle, silent beyond 32 squares
	TestEqual(TEXT("full volume at the source"), UMRAudioSubsystem::VolumeAtDistance(0.f, 0.07447f, 70.4f), 1.f);
	TestEqual(TEXT("about a sixth at 32 squares"), UMRAudioSubsystem::VolumeAtDistance(7000.f, 0.07447f, 70.4f), 0.161f, 0.005f);
	TestEqual(TEXT("silent beyond 32 squares"), UMRAudioSubsystem::VolumeAtDistance(7100.f, 0.07447f, 70.4f), 0.f);
	TestEqual(TEXT("asset names"), UMRAudioSubsystem::AssetName(TEXT("Rs_wind.ogg")), FString(TEXT("Rs_wind")));
	TestEqual(TEXT("asset names (other characters)"), UMRAudioSubsystem::AssetName(TEXT("a-b c.ogg")), FString(TEXT("a_b_c")));

	// data/audio/rooms.json (tools/audio/extract_audio.py) against the Kod
	const TSharedPtr<FJsonObject> Rooms = ReadAudioJson(TEXT("rooms.json"), TEXT("rooms"));
	if (!TestTrue(TEXT("data/audio/rooms.json"), Rooms.IsValid()))
	{
		return false;
	}
	auto Room = [&](const TCHAR* Rid) { const TSharedPtr<FJsonObject>* R = nullptr; return Rooms->TryGetObjectField(Rid, R) ? *R : nullptr; };
	auto FirstLoop = [](const TSharedPtr<FJsonObject>& R)
	{
		const TArray<TSharedPtr<FJsonValue>>* Loops = nullptr;
		return R.IsValid() && R->TryGetArrayField(TEXT("loops"), Loops) && Loops->Num() > 0 ? (*Loops)[0]->AsObject()->GetStringField(TEXT("sound")) : FString();
	};
	auto Periodic = [](const TSharedPtr<FJsonObject>& R, int32& OutCount)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		OutCount = R.IsValid() && R->TryGetObjectField(TEXT("periodic"), P) ? (*P)->GetArrayField(TEXT("sounds")).Num() : 0;
		return P ? (*P)->GetNumberField(TEXT("interval_ms")) : 0.0;
	};
	int32 Count = 0;
	const TSharedPtr<FJsonObject> Raza = Room(TEXT("300"));
	TestEqual(TEXT("Raza plays the title theme"), Raza.IsValid() ? Raza->GetStringField(TEXT("music")) : FString(), FString(TEXT("login.ogg")));
	TestEqual(TEXT("Raza's country ambience"), FirstLoop(Raza), FString(TEXT("ambcntry.ogg")));
	TestEqual(TEXT("Raza's periodic interval (forest and lake: 20 s / 4)"), Periodic(Raza, Count), 5000.0);
	TestEqual(TEXT("Raza's periodic sounds (7 birds, 7 shore, 2 waterfront)"), Count, 16);
	const TSharedPtr<FJsonObject> Smithy = Room(TEXT("303"));
	TestEqual(TEXT("the smithy's music"), Smithy.IsValid() ? Smithy->GetStringField(TEXT("music")) : FString(), FString(TEXT("smithy.ogg")));
	TestEqual(TEXT("the smithy's fire"), FirstLoop(Smithy), FString(TEXT("fireplac.ogg")));
	const TSharedPtr<FJsonObject> Crypt = Room(TEXT("306"));
	TestEqual(TEXT("the crypt's music"), Crypt.IsValid() ? Crypt->GetStringField(TEXT("music")) : FString(), FString(TEXT("nec03.ogg")));
	TestEqual(TEXT("the crypt's cave"), FirstLoop(Crypt), FString(TEXT("ambcave.ogg")));
	TestEqual(TEXT("the crypt's periodic interval (necropolis: 20 s / 2)"), Periodic(Crypt, Count), 10000.0);
	TestEqual(TEXT("the crypt's periodic sounds (9 necropolis, a drop)"), Count, 10);
	const TSharedPtr<FJsonObject> Inn = Room(TEXT("301"));
	TestEqual(TEXT("no loops in the inn"), FirstLoop(Inn), FString());
	TestEqual(TEXT("no periodic sounds in the inn"), Periodic(Inn, Count), 0.0);

	// every sound the data names was imported (tools/ue/build_audio.py)
	const TSharedPtr<FJsonObject> Sounds = ReadAudioJson(TEXT("sounds.json"), TEXT("sounds"));
	if (TestTrue(TEXT("data/audio/sounds.json"), Sounds.IsValid()))
	{
		TArray<FString> Missing;
		for (const auto& Pair : Sounds->Values)
		{
			bool bMissing = false;
			if (Pair.Value->AsObject()->TryGetBoolField(TEXT("missing"), bMissing) && bMissing)
			{
				continue;  // named by the Kod, not shipped with the original client
			}
			const FString File = Pair.Value->AsObject()->GetStringField(TEXT("file"));
			const FString Name = UMRAudioSubsystem::AssetName(File);
			if (!LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Generated/Audio/Original/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Missing.Add(File);
			}
		}
		TestEqual(FString::Printf(TEXT("sounds not imported: %s"), *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);
	}
	return true;
}

#endif
