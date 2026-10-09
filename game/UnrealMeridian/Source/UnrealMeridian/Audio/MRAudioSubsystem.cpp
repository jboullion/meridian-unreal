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
#include "UnrealMeridian.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundAttenuation.h"
#include "Audio/MRServerSound.h"
#include "Engine/GameInstance.h"
#include "Net/MRAssetCache.h"
#include "Net/MRNetSubsystem.h"
#include "TimerManager.h"
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
	TAutoConsoleVariable<int32> CVarBackground(TEXT("mr.Audio.Background"), 1,
		TEXT("1: keep playing sound when the game window isn't focused (the engine mutes it by default); 0: mute."));
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
	if (IsValid(Music))
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

USoundBase* UMRAudioSubsystem::FindImported(const FString& File)
{
	const FString Key = File.ToLower();
	if (const TWeakObjectPtr<USoundBase>* Cached = Sounds.Find(Key); Cached && Cached->IsValid())
	{
		return Cached->Get();
	}
	if (Missing.Contains(Key))
	{
		return nullptr;
	}
	const FString Name = AssetName(File);
	const FString Path = FString::Printf(TEXT("%s%s.%s"), SoundDir, *Name, *Name);
	USoundBase* S = FindObject<USoundBase>(nullptr, *Path);
	S = S ? S : LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (S)
	{
		Sounds.Add(Key, S);
	}
	else
	{
		Missing.Add(Key);
	}
	return S;
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
	TObjectPtr<UAudioComponent>& Loop = NamedLoops.FindOrAdd(Key);
	if (Volume <= 0.01f)
	{
		if (IsValid(Loop) && Loop->IsPlaying())
		{
			Loop->FadeOut(FadeSeconds, 0.f);
		}
		return;
	}
	if (!IsValid(Loop))
	{
		USoundBase* Sound = FindSound(File);
		if (!Sound)
		{
			return;
		}
		Loop = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, true, false);
		if (!IsValid(Loop))
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
	if (Want.Equals(MusicFile, ESearchCase::IgnoreCase) && (Want.IsEmpty() || IsValid(Music)))
	{
		return;  // the same track carries on (the original: MusicPlayFile ignores the current file)
	}
	SetMusicSound(Want, Want.IsEmpty() ? nullptr : FindSound(Want));
}

void UMRAudioSubsystem::SetMusicSound(const FString& Want, USoundBase* Sound)
{
	const float Fade = float(Number(TEXT("music_fade_s"), 1.5));
	if (IsValid(Music))
	{
		Music->FadeOut(Fade, 0.f);
		Music = nullptr;
	}
	MusicFile = Want;
	if (Want.IsEmpty())
	{
		return;
	}
	if (Sound)
	{
		Music = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, true, true);
		if (IsValid(Music))
		{
			Music->FadeIn(Fade, 1.f);
			UE_LOG(LogMeridian, Log, TEXT("MRAudio: music %s"), *Want);
		}
	}
}

void UMRAudioSubsystem::StopLoops()
{
	const float Fade = float(Number(TEXT("loop_fade_s"), 0.5));
	for (const TObjectPtr<UAudioComponent>& Loop : Loops)
	{
		if (IsValid(Loop))
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

void UMRAudioSubsystem::SetServerDriven(bool bOn)
{
	if (bOn == bServerDriven)
	{
		return;
	}
	bServerDriven = bOn;
	if (bOn)
	{
		StopLoops();  // the zone's own; the server sends the room's
		SetMusicSound(FString(), nullptr);
	}
	else
	{
		ServerStopLoops();
		Zone = -1;  // the zone's sound again on the next tick
	}
}

void UMRAudioSubsystem::ResolveServer(const FString& InFile, bool bLoop, TFunction<void(USoundBase*)> Done)
{
	// imported (the zones' sounds and music), else the server's file decoded once; the rsc may name
	// a .wav the server serves as .ogg
	FString File = InFile.ToLower();
	if (USoundBase* S = FindImported(File))
	{
		Done(S);
		return;
	}
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMRNetSubsystem* Net = GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	FMRAssetCache* Cache = Net ? Net->GetAssets() : nullptr;
	if (Cache && !Cache->IsListed(File) && File.EndsWith(TEXT(".wav")))
	{
		File = FPaths::ChangeExtension(File, TEXT("ogg"));
		if (USoundBase* S = FindImported(File))
		{
			Done(S);
			return;
		}
	}
	TWeakObjectPtr<UMRAudioSubsystem> Weak(this);
	auto Make = [Weak, bLoop, Done](TSharedPtr<const FMRPcmSound> Pcm)
	{
		UMRAudioSubsystem* Self = Weak.Get();
		if (!Self || !Pcm.IsValid())
		{
			return;
		}
		UMRServerSoundWave* Wave = NewObject<UMRServerSoundWave>(Self);
		Wave->Init(Pcm, bLoop);
		Done(Wave);
	};
	if (const TSharedPtr<const FMRPcmSound>* Known = Decoded.Find(File))
	{
		Make(*Known);
		return;
	}
	if (Undecodable.Contains(File) || !Cache || !Cache->IsListed(File))
	{
		if (!Undecodable.Contains(File))
		{
			Undecodable.Add(File);
			UE_LOG(LogMeridian, Log, TEXT("MRAudio: the server has no %s"), *File);
		}
		return;
	}
	TArray<TFunction<void(TSharedPtr<const FMRPcmSound>)>>& Queue = Waiting.FindOrAdd(File);
	Queue.Add(Make);
	if (Queue.Num() > 1)
	{
		return;  // already on its way
	}
	Cache->Fetch(File, [Weak, File](bool bOk, const TArray<uint8>& Bytes)
	{
		UMRAudioSubsystem* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		FString Error;
		TSharedPtr<const FMRPcmSound> Pcm = bOk ? MRServerSound::Decode(Bytes, Error) : nullptr;
		if (Pcm.IsValid())
		{
			Self->Decoded.Add(File, Pcm);
			UE_LOG(LogMeridian, Log, TEXT("MRAudio: decoded the server's %s (%.1f s)"), *File, Pcm->DurationSeconds());
		}
		else
		{
			Self->Undecodable.Add(File);
			UE_LOG(LogMeridian, Warning, TEXT("MRAudio: %s: %s"), *File, bOk ? *Error : TEXT("not downloaded"));
		}
		TArray<TFunction<void(TSharedPtr<const FMRPcmSound>)>> Done;
		Self->Waiting.RemoveAndCopyValue(File, Done);
		for (const TFunction<void(TSharedPtr<const FMRPcmSound>)>& F : Done)
		{
			F(Pcm);
		}
	});
}

void UMRAudioSubsystem::ServerMusic(const FString& File)
{
	const FString Want = bMusicOn ? File.ToLower() : FString();
	if (Want.Equals(MusicFile, ESearchCase::IgnoreCase) && (Want.IsEmpty() || IsValid(Music)))
	{
		return;
	}
	if (Want.IsEmpty())
	{
		SetMusicSound(FString(), nullptr);
		return;
	}
	MusicFile = Want;  // (asked for: the same request again while it decodes is ignored)
	TWeakObjectPtr<UMRAudioSubsystem> Weak(this);
	ResolveServer(Want, true, [Weak, Want](USoundBase* Sound)
	{
		UMRAudioSubsystem* Self = Weak.Get();
		if (Self && Self->MusicFile.Equals(Want, ESearchCase::IgnoreCase))
		{
			if (UMRServerSoundWave* Wave = Cast<UMRServerSoundWave>(Sound))
			{
				Wave->SoundClassObject = Self->Classes.FindRef(TEXT("SC_Music"));
			}
			Self->MusicFile.Reset();
			Self->SetMusicSound(Want, Sound);
		}
	});
}

void UMRAudioSubsystem::ServerSound(const FString& File, const FVector& Location, bool b2D, bool bLoop, float Pitch, uint32 ObjectId)
{
	++ServerAsked;
	if (bLoop && !bLoopsOn)
	{
		return;  // the original: no looping sounds when they're switched off
	}
	TWeakObjectPtr<UMRAudioSubsystem> Weak(this);
	const FString Key = File.ToLower();
	ResolveServer(Key, bLoop, [Weak, Key, Location, b2D, bLoop, Pitch, ObjectId](USoundBase* Sound)
	{
		UMRAudioSubsystem* Self = Weak.Get();
		if (!Self || !Sound)
		{
			return;
		}
		if (UMRServerSoundWave* Wave = Cast<UMRServerSoundWave>(Sound))
		{
			Wave->SoundClassObject = Self->Classes.FindRef(bLoop ? TEXT("SC_Loops") : TEXT("SC_World"));
		}
		++Self->ServerPlayed;
		UAudioComponent* C = Self->Create(Sound, Location, b2D);
		if (!C)
		{
			return;  // no audio device (-nosound)
		}
		C->SetPitchMultiplier(Pitch);
		// an imported sound loops only if it was imported to; the server says
		if (!Cast<UMRServerSoundWave>(Sound) && bLoop)
		{
			C->bAutoDestroy = false;
			C->OnAudioFinishedNative.AddWeakLambda(Self, [](UAudioComponent* Done) { if (IsValid(Done)) Done->Play(); });
		}
		C->Play();
		Self->ServerSounds.Add(C);
		Self->ServerSoundKeys.Add({Key, ObjectId});
		Self->ServerSoundLoops.Add(bLoop);
		// a decoded one-shot doesn't end by itself (a procedural wave): stop it at its end
		if (UMRServerSoundWave* Wave = Cast<UMRServerSoundWave>(Sound); Wave && !bLoop && Self->GetWorld())
		{
			FTimerHandle Handle;
			TWeakObjectPtr<UAudioComponent> WeakC(C);
			Self->GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Self, [WeakC]()
			{
				if (WeakC.IsValid())
				{
					WeakC->Stop();
				}
			}), FMath::Max(0.05f, Wave->Duration / FMath::Max(0.25f, Pitch)) + 0.05f, false);
		}
		UE_LOG(LogMeridian, Verbose, TEXT("MRAudio: server sound %s%s"), *Key, bLoop ? TEXT(" (loop)") : TEXT(""));
	});
}

void UMRAudioSubsystem::ServerStopSound(const FString& File, uint32 ObjectId)
{
	const FString Key = File.ToLower();
	for (int32 i = ServerSounds.Num() - 1; i >= 0; --i)
	{
		if (ServerSoundKeys[i].Key == Key && (ObjectId == 0 || ServerSoundKeys[i].Value == ObjectId))
		{
			if (IsValid(ServerSounds[i]))
			{
				ServerSounds[i]->OnAudioFinishedNative.RemoveAll(this);
				ServerSounds[i]->Stop();
			}
			ServerSounds.RemoveAt(i);
			ServerSoundKeys.RemoveAt(i);
			ServerSoundLoops.RemoveAt(i);
		}
	}
}

void UMRAudioSubsystem::ServerStopLoops()
{
	const float Fade = float(Number(TEXT("loop_fade_s"), 0.5));
	for (int32 i = ServerSounds.Num() - 1; i >= 0; --i)
	{
		// a loop, or a one-shot over: either goes from the list
		const bool bOver = !IsValid(ServerSounds[i]) || !ServerSounds[i]->IsPlaying();
		if (ServerSoundLoops[i] || bOver)
		{
			if (!bOver)
			{
				ServerSounds[i]->OnAudioFinishedNative.RemoveAll(this);
				ServerSounds[i]->bAutoDestroy = true;
				ServerSounds[i]->FadeOut(Fade, 0.f);
			}
			ServerSounds.RemoveAt(i);
			ServerSoundKeys.RemoveAt(i);
			ServerSoundLoops.RemoveAt(i);
		}
	}
}

void UMRAudioSubsystem::EnterZone(int32 InZone)
{
	Zone = InZone;
	if (bServerDriven)
	{
		return;  // the server sends the room's music and sounds
	}
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
	FApp::SetUnfocusedVolumeMultiplier(CVarBackground.GetValueOnGameThread() != 0 ? 1.f : 0.f);
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
	if (bServerDriven)
	{
		if (!bMusicOn)
		{
			SetMusicSound(FString(), nullptr);
		}
		if (!bLoopsOn)
		{
			ServerStopLoops();
		}
	}
	else if (Zone >= 0)
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
		const FString Key = FString::Printf(TEXT("%d %d %d %d %d %d %d"), CVarMusic.GetValueOnGameThread(), CVarMusicVolume.GetValueOnGameThread(),
			CVarSound.GetValueOnGameThread(), CVarSoundVolume.GetValueOnGameThread(), CVarLoops.GetValueOnGameThread(), CVarRandom.GetValueOnGameThread(),
			CVarBackground.GetValueOnGameThread());
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
	if (Zone > 0 && !bServerDriven)
	{
		TickPeriodic(Now);
	}
	// the zone's track should always be playing (it loops): if anything stopped it, say so and
	// start it again (not without an audio device: -nosound)
	if (Now >= NextMusicCheck && !MusicFile.IsEmpty() && World->GetAudioDeviceRaw())
	{
		NextMusicCheck = Now + 1.0;
		if (IsValid(Music) && !Music->IsPlaying())
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRAudio: music %s stopped by itself; restarting it"), *MusicFile);
			const FString Track = MusicFile;
			MusicFile.Reset();
			if (bServerDriven)
			{
				ServerMusic(Track);
			}
			else
			{
				SetMusic(Track);
			}
		}
	}
}

TStatId UMRAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRAudioSubsystem, STATGROUP_Tickables);
}
