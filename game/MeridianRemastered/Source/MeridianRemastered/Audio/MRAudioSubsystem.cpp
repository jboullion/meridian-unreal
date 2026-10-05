#include "Audio/MRAudioSubsystem.h"

#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	const TCHAR* SoundDir = TEXT("/Game/Generated/Audio/Original/");
	const TCHAR* MixDir = TEXT("/Game/Generated/Audio/Mix/");
	constexpr double RooPerSquare = 1024.0;

	// the original client's settings (clientd3d config.c): on/off and 0..100
	TAutoConsoleVariable<int32> CVarMusic(TEXT("mr.Audio.Music"), 1, TEXT("0: no music (the original's setting)."));
	TAutoConsoleVariable<int32> CVarMusicVolume(TEXT("mr.Audio.MusicVolume"), 100, TEXT("Music volume, 0..100."));
	TAutoConsoleVariable<int32> CVarSound(TEXT("mr.Audio.Sound"), 1, TEXT("0: no sounds but music."));
	TAutoConsoleVariable<int32> CVarSoundVolume(TEXT("mr.Audio.SoundVolume"), 100, TEXT("Sound volume (everything but music), 0..100."));
	TAutoConsoleVariable<int32> CVarLoops(TEXT("mr.Audio.Loops"), 1, TEXT("0: no looping room sounds (the original's setting)."));
	TAutoConsoleVariable<int32> CVarRandom(TEXT("mr.Audio.Random"), 1, TEXT("0: no random (periodic) room sounds (the original's setting)."));

	FAutoConsoleCommandWithWorld CmdReload(
		TEXT("MRAudioReload"), TEXT("Re-read data/audio and restart the zone's sound."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UMRAudioSubsystem* Audio = World ? World->GetSubsystem<UMRAudioSubsystem>() : nullptr)
			{
				Audio->Reload();
			}
		}));

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Obj;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Obj);
		}
		if (!Obj.IsValid())
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRAudio: could not read %s (python tools/audio/extract_audio.py)"), *Path);
		}
		return Obj;
	}
}

FString UMRAudioSubsystem::AssetName(const FString& File)
{
	FString Name = FPaths::GetBaseFilename(File);
	for (TCHAR& C : Name)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
		{
			C = TEXT('_');
		}
	}
	return Name;
}

float UMRAudioSubsystem::VolumeAtDistance(float DistanceCm, float RolloffPerM, float MaxDistanceM)
{
	const float M = DistanceCm / 100.f;
	return M > MaxDistanceM ? 0.f : 1.f / (1.f + RolloffPerM * FMath::Max(0.f, M));
}

bool UMRAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMRAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMRAudioSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	Reload();
}

void UMRAudioSubsystem::Deinitialize()
{
	StopLoops();
	if (Music.IsValid())
	{
		Music->Stop();
	}
	Super::Deinitialize();
}

void UMRAudioSubsystem::Reload()
{
	const FString Dir = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("audio"));
	const TSharedPtr<FJsonObject> RoomsFile = ReadJson(FPaths::Combine(Dir, TEXT("rooms.json")));
	const TSharedPtr<FJsonObject>* RoomMap = nullptr;
	Rooms = RoomsFile.IsValid() && RoomsFile->TryGetObjectField(TEXT("rooms"), RoomMap) ? *RoomMap : nullptr;
	Config = ReadJson(FPaths::Combine(Dir, TEXT("audio.json")));
	AttenuationAsset = nullptr;
	SettingsKey.Reset();
	StopLoops();
	MusicFile.Reset();  // a reload may change the track
	Zone = -1;
	Preload();
}

void UMRAudioSubsystem::Preload()
{
	TSet<FString> Files;
	if (Rooms.IsValid())
	{
		for (const auto& Pair : Rooms->Values)
		{
			const TSharedPtr<FJsonObject> R = Pair.Value->AsObject();
			FString File;
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (R.IsValid() && R->TryGetStringField(TEXT("music"), File))
			{
				Files.Add(File);
			}
			if (R.IsValid() && R->TryGetArrayField(TEXT("loops"), Items))
			{
				for (const TSharedPtr<FJsonValue>& L : *Items)
				{
					Files.Add(L->AsObject()->GetStringField(TEXT("sound")));
				}
			}
			if (R.IsValid() && R->TryGetObjectField(TEXT("periodic"), P) && (*P)->TryGetArrayField(TEXT("sounds"), Items))
			{
				for (const TSharedPtr<FJsonValue>& S : *Items)
				{
					Files.Add(S->AsString());
				}
			}
		}
	}
	TArray<FSoftObjectPath> Paths;
	for (const FString& File : Files)
	{
		const FString Name = AssetName(File);
		Paths.Emplace(FString::Printf(TEXT("%s%s.%s"), SoundDir, *Name, *Name));
	}
	if (Paths.Num() > 0 && UAssetManager::IsInitialized())
	{
		Preloaded = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
	}
}

double UMRAudioSubsystem::Number(const TCHAR* Field, double Default) const
{
	double Value = Default;
	return Config.IsValid() && Config->TryGetNumberField(Field, Value) ? Value : Default;
}

TSharedPtr<FJsonObject> UMRAudioSubsystem::Room(int32 InZone) const
{
	const TSharedPtr<FJsonObject>* Out = nullptr;
	return Rooms.IsValid() && Rooms->TryGetObjectField(FString::FromInt(InZone), Out) ? *Out : nullptr;
}

USoundBase* UMRAudioSubsystem::FindSound(const FString& File)
{
	if (File.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = File.ToLower();
	TWeakObjectPtr<USoundBase>& Cached = Sounds.FindOrAdd(Key);
	if (!Cached.IsValid() && !Missing.Contains(Key))
	{
		const FString Name = AssetName(File);
		const FString Path = FString::Printf(TEXT("%s%s.%s"), SoundDir, *Name, *Name);
		// preloaded (Preload) or already in memory; else a load now (a sound the rooms don't name)
		Cached = FindObject<USoundBase>(nullptr, *Path);
		if (!Cached.IsValid())
		{
			Cached = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Cached.IsValid())
		{
			Missing.Add(Key);
			UE_LOG(LogMeridian, Warning, TEXT("MRAudio: no sound %s (python tools/audio/extract_audio.py, then build_world.ps1)"), *File);
		}
	}
	return Cached.Get();
}

USoundAttenuation* UMRAudioSubsystem::Attenuation()
{
	if (!AttenuationAsset)
	{
		// the original client (irrKlang): rolloff 0.00016 per fine unit, silent beyond 32 squares;
		// UE's custom curve runs over the normalised distance to FalloffDistance
		const float Rolloff = float(Number(TEXT("rolloff_per_m"), 0.07447));
		const float MaxM = float(Number(TEXT("max_distance_m"), 70.4));
		AttenuationAsset = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& S = AttenuationAsset->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector::ZeroVector;
		S.FalloffDistance = MaxM * 100.f;
		S.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
		FRichCurve* Curve = S.CustomAttenuationCurve.GetRichCurve();
		Curve->Reset();
		constexpr int32 Keys = 24;
		for (int32 i = 0; i <= Keys; ++i)
		{
			const float X = 0.995f * float(i) / Keys;
			Curve->AddKey(X, VolumeAtDistance(X * MaxM * 100.f, Rolloff, MaxM));
		}
		Curve->AddKey(1.f, 0.f);
	}
	return AttenuationAsset;
}

UAudioComponent* UMRAudioSubsystem::Create(USoundBase* Sound, const FVector& Location, bool b2D)
{
	// not playing yet, destroyed when it finishes: the caller plays or fades it in
	UWorld* World = GetWorld();
	if (!Sound || !World)
	{
		return nullptr;
	}
	FAudioDevice::FCreateComponentParams Params(World);
	Params.bAutoDestroy = true;
	Params.bPlay = false;
	if (!b2D)
	{
		Params.SetLocation(Location);
		Params.AttenuationSettings = Attenuation();
	}
	UAudioComponent* C = FAudioDevice::CreateComponent(Sound, Params);
	if (C && b2D)
	{
		C->bAllowSpatialization = false;
		C->bIsUISound = false;
	}
	return C;
}

UAudioComponent* UMRAudioSubsystem::PlayOriginal(const FString& File, const FVector& Location, bool b2D, float Volume, float Pitch, bool bMuffled)
{
	USoundBase* Sound = FindSound(File);
	UWorld* World = GetWorld();
	if (!Sound || !World)
	{
		return nullptr;
	}
	UAudioComponent* C = Create(Sound, Location, b2D);
	if (!C)
	{
		return nullptr;
	}
	C->SetVolumeMultiplier(Volume);
	C->SetPitchMultiplier(Pitch);
	if (bMuffled)
	{
		C->SetLowPassFilterEnabled(true);
		C->SetLowPassFilterFrequency(800.f);
	}
	C->Play();
	return C;
}

UAudioComponent* UMRAudioSubsystem::Play(const UObject* WorldContext, const FString& File, FVector Location, bool b2D, float Volume, float Pitch)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UMRAudioSubsystem* Audio = World ? World->GetSubsystem<UMRAudioSubsystem>() : nullptr;
	return Audio ? Audio->PlayOriginal(File, Location, b2D, Volume, Pitch) : nullptr;
}

void UMRAudioSubsystem::SetLoop2D(FName Key, const FString& File, float Volume, bool bMuffled, float FadeSeconds)
{
	TWeakObjectPtr<UAudioComponent>& Loop = NamedLoops.FindOrAdd(Key);
	if (Volume <= 0.01f)
	{
		if (Loop.IsValid() && Loop->IsPlaying())
		{
			Loop->FadeOut(FadeSeconds, 0.f);
		}
		return;
	}
	if (!Loop.IsValid())
	{
		USoundBase* Sound = FindSound(File);
		if (!Sound)
		{
			return;
		}
		Loop = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, true, false);
		if (!Loop.IsValid())
		{
			return;
		}
		UE_LOG(LogMeridian, Log, TEXT("MRAudio: loop %s (%s)"), *File, *Key.ToString());
	}
	Loop->SetLowPassFilterEnabled(bMuffled);
	Loop->SetLowPassFilterFrequency(900.f);
	if (!Loop->IsPlaying())
	{
		Loop->FadeIn(FadeSeconds, Volume);
	}
	else
	{
		Loop->AdjustVolume(1.f, Volume);
	}
}

int32 UMRAudioSubsystem::ViewZone() const
{
	// as the environment director: the zone the camera is in (look-dev and spectating move the
	// camera without the player), else the player's
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		if (const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>())
		{
			if (const int32 Rid = Zones->ZoneAtLocation(PC->PlayerCameraManager->GetCameraLocation()))
			{
				return Rid;
			}
		}
	}
	const AMRPlayerState* PS = PC ? PC->GetPlayerState<AMRPlayerState>() : nullptr;
	return PS ? PS->GetZoneId() : -1;
}

void UMRAudioSubsystem::SetMusic(const FString& File)
{
	const FString Want = bMusicOn ? File : FString();
	if (Want.Equals(MusicFile, ESearchCase::IgnoreCase) && (Want.IsEmpty() || Music.IsValid()))
	{
		return;  // the same track carries on (the original: MusicPlayFile ignores the current file)
	}
	const float Fade = float(Number(TEXT("music_fade_s"), 1.5));
	if (Music.IsValid())
	{
		Music->FadeOut(Fade, 0.f);
		Music.Reset();
	}
	MusicFile = Want;
	if (Want.IsEmpty())
	{
		return;
	}
	if (USoundBase* Sound = FindSound(Want))
	{
		Music = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, true, true);
		if (Music.IsValid())
		{
			Music->FadeIn(Fade, 1.f);
			UE_LOG(LogMeridian, Log, TEXT("MRAudio: music %s"), *Want);
		}
	}
}

void UMRAudioSubsystem::StopLoops()
{
	const float Fade = float(Number(TEXT("loop_fade_s"), 0.5));
	for (const TWeakObjectPtr<UAudioComponent>& Loop : Loops)
	{
		if (Loop.IsValid())
		{
			Loop->FadeOut(Fade, 0.f);
		}
	}
	Loops.Reset();
}

void UMRAudioSubsystem::StartLoops(int32 InZone)
{
	StopLoops();
	const TSharedPtr<FJsonObject> R = Room(InZone);
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const FMRZoneInfo* Info = Zones ? Zones->FindZone(InZone) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!bLoopsOn || !R.IsValid() || !Info || !R->TryGetArrayField(TEXT("loops"), Items))
	{
		return;
	}
	const float Fade = float(Number(TEXT("loop_fade_s"), 0.5));
	const double Height = Number(TEXT("loop_height_m"), 1.5);
	for (const TSharedPtr<FJsonValue>& Item : *Items)
	{
		const TSharedPtr<FJsonObject> L = Item->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
		FString File;
		if (!L.IsValid() || !L->TryGetStringField(TEXT("sound"), File) || !L->TryGetArrayField(TEXT("pos_m"), Pos) || Pos->Num() < 2)
		{
			continue;
		}
		// zone metres (x east, z south) and the floor's height -> world cm
		double Floor = 0.0;
		L->TryGetNumberField(TEXT("floor_m"), Floor);
		const FVector At = Info->Origin + FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), Floor + Height) * 100.0;
		if (UAudioComponent* C = Create(FindSound(File), At, false))
		{
			C->FadeIn(Fade, 1.f);
			Loops.Add(C);
			UE_LOG(LogMeridian, Log, TEXT("MRAudio: loop %s at %s (zone %d)"), *File, *At.ToCompactString(), InZone);
		}
	}
}

void UMRAudioSubsystem::EnterZone(int32 InZone)
{
	Zone = InZone;
	const TSharedPtr<FJsonObject> R = Room(InZone);
	FString Track;
	if (R.IsValid())
	{
		R->TryGetStringField(TEXT("music"), Track);
	}
	SetMusic(Track);
	StartLoops(InZone);
	NextPeriodic = FPlatformTime::Seconds() + FMath::FRandRange(1.0, 3.0);
}

void UMRAudioSubsystem::TickPeriodic(double Now)
{
	if (Now < NextPeriodic)
	{
		return;
	}
	const TSharedPtr<FJsonObject> R = Room(Zone);
	const TSharedPtr<FJsonObject>* P = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	double Interval = 20000.0;
	if (!R.IsValid() || !R->TryGetObjectField(TEXT("periodic"), P) || !(*P)->TryGetArrayField(TEXT("sounds"), List) || List->Num() == 0)
	{
		NextPeriodic = Now + 5.0;
		return;
	}
	(*P)->TryGetNumberField(TEXT("interval_ms"), Interval);
	const double Jitter = Number(TEXT("periodic_jitter"), 0.2);
	NextPeriodic = Now + Interval / 1000.0 * FMath::FRandRange(1.0 - Jitter, 1.0 + Jitter);
	if (!bRandomOn)
	{
		return;
	}
	// the original: a random sound of the list at a random square of the room
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const FMRZoneInfo* Info = Zones ? Zones->FindZone(Zone) : nullptr;
	if (!Info)
	{
		return;
	}
	const int32 Rows = FMath::Max(1, FMath::FloorToInt32(Info->GridSizeRoo.Y / RooPerSquare));
	const int32 Cols = FMath::Max(1, FMath::FloorToInt32(Info->GridSizeRoo.X / RooPerSquare));
	const FVector At = Zones->GridToWorld(Zone, FMath::RandRange(1, Rows), FMath::RandRange(1, Cols), true)
		+ FVector(0.0, 0.0, Number(TEXT("loop_height_m"), 1.5) * 100.0);
	const FString File = (*List)[FMath::RandRange(0, List->Num() - 1)]->AsString();
	bool bPitch = false;
	if (Config.IsValid())
	{
		Config->TryGetBoolField(TEXT("random_pitch"), bPitch);
	}
	if (PlayOriginal(File, At, false, 1.f, bPitch ? FMath::FRandRange(0.85f, 1.15f) : 1.f))
	{
		UE_LOG(LogMeridian, Log, TEXT("MRAudio: periodic %s at %s"), *File, *At.ToCompactString());
	}
}

void UMRAudioSubsystem::ApplySettings()
{
	bMusicOn = CVarMusic.GetValueOnGameThread() != 0;
	bLoopsOn = CVarLoops.GetValueOnGameThread() != 0;
	bRandomOn = CVarRandom.GetValueOnGameThread() != 0;
	const float MusicVol = bMusicOn ? FMath::Clamp(CVarMusicVolume.GetValueOnGameThread(), 0, 100) / 100.f : 0.f;
	const float SoundVol = CVarSound.GetValueOnGameThread() != 0 ? FMath::Clamp(CVarSoundVolume.GetValueOnGameThread(), 0, 100) / 100.f : 0.f;
	UWorld* World = GetWorld();
	if (!Mix)
	{
		Mix = LoadObject<USoundMix>(nullptr, *FString::Printf(TEXT("%sSM_Settings.SM_Settings"), MixDir), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	const TSharedPtr<FJsonObject>* Levels = nullptr;
	if (Config.IsValid())
	{
		Config->TryGetObjectField(TEXT("volumes"), Levels);
	}
	for (const TCHAR* Name : {TEXT("SC_Music"), TEXT("SC_Loops"), TEXT("SC_Periodic"), TEXT("SC_Weather"), TEXT("SC_Combat"),
		TEXT("SC_Spells"), TEXT("SC_World"), TEXT("SC_UI")})
	{
		TObjectPtr<USoundClass>& Class = Classes.FindOrAdd(Name);
		if (!Class)
		{
			Class = LoadObject<USoundClass>(nullptr, *FString::Printf(TEXT("%s%s.%s"), MixDir, Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Class || !Mix)
		{
			continue;
		}
		double Level = 1.0;
		if (Levels)
		{
			(*Levels)->TryGetNumberField(Name, Level);
		}
		const FString N(Name);
		float Volume = float(Level) * (N == TEXT("SC_Music") ? MusicVol : SoundVol);
		Volume *= N == TEXT("SC_Loops") && !bLoopsOn ? 0.f : 1.f;
		Volume *= N == TEXT("SC_Periodic") && !bRandomOn ? 0.f : 1.f;
		UGameplayStatics::SetSoundMixClassOverride(World, Mix, Class, Volume, 1.f, 0.25f, true);
	}
	if (Mix)
	{
		UGameplayStatics::PushSoundMixModifier(World, Mix);
	}
	// switching loops or music off stops them (the original doesn't play them at all)
	if (Zone >= 0)
	{
		const TSharedPtr<FJsonObject> R = Room(Zone);
		FString Track;
		if (R.IsValid())
		{
			R->TryGetStringField(TEXT("music"), Track);
		}
		SetMusic(Track);
		if (!bLoopsOn)
		{
			StopLoops();
		}
		else if (Loops.Num() == 0)
		{
			StartLoops(Zone);
		}
	}
}

void UMRAudioSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay() || !Rooms.IsValid())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextSettings)
	{
		NextSettings = Now + 0.25;
		const FString Key = FString::Printf(TEXT("%d %d %d %d %d %d"), CVarMusic.GetValueOnGameThread(), CVarMusicVolume.GetValueOnGameThread(),
			CVarSound.GetValueOnGameThread(), CVarSoundVolume.GetValueOnGameThread(), CVarLoops.GetValueOnGameThread(), CVarRandom.GetValueOnGameThread());
		if (Key != SettingsKey)
		{
			SettingsKey = Key;
			ApplySettings();
		}
	}
	const int32 Now_Zone = ViewZone();
	if (Now_Zone != Zone && Now_Zone > 0)
	{
		EnterZone(Now_Zone);
	}
	if (Zone > 0)
	{
		TickPeriodic(Now);
	}
}

TStatId UMRAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRAudioSubsystem, STATGROUP_Tickables);
}
