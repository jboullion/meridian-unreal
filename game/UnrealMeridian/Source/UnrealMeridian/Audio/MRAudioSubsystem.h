#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundClass;
class USoundMix;

/**
 * The client's sound (docs/adr/0006): the original's sounds played by the original's rules, from
 * data/audio/rooms.json (tools/audio/extract_audio.py) and data/audio/audio.json. On clients only
 * (never the dedicated server), in game and PIE worlds.
 *
 *  - Music: the zone's track (kod prMusic), crossfaded over music_fade_s when it changes; the same
 *    track carries on across zones, as in the original.
 *  - Loops: the zone's looping sounds (its own and its terrain's ambience) at their squares, with the
 *    original's attenuation (volume 1 / (1 + rolloff x distance), silent beyond 32 squares);
 *    started on entering the zone, faded out on leaving.
 *  - Periodic sounds: every interval (+-20%) one of the zone's terrain sounds at a random square.
 *  - Named 2D loops and one-shots for others to drive (the environment director's rain, wind and
 *    thunder), and Play() for one-shots anywhere (gameplay sounds, later from Gameplay Cues).
 *  - The original's settings as console variables: mr.Audio.Music / MusicVolume (0..100),
 *    mr.Audio.Sound / SoundVolume, mr.Audio.Loops, mr.Audio.Random; applied through the sound
 *    classes (tools/ue/build_audio.py) with SM_Settings. mr.Audio.Background 1 (default) keeps sound
 *    playing while the window isn't focused (the engine mutes it otherwise).
 * Every sound it starts is logged ("MRAudio: ..."). Console: MRAudioReload re-reads the data.
 */
UCLASS()
class UNREALMERIDIAN_API UMRAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** "Rs_wind.ogg" -> "Rs_wind": the asset name build_audio.py imports it as. */
	static FString AssetName(const FString& File);

	/** The original's 3D volume at a distance (cm): 1 / (1 + rolloff x metres), 0 beyond the max. Pure, for tests. */
	static float VolumeAtDistance(float DistanceCm, float RolloffPerM, float MaxDistanceM);

	/** An original sound by file name ("ambcntry.ogg"), or null if it wasn't imported. */
	USoundBase* FindSound(const FString& File);

	/** Play an original sound once: at Location with the original's attenuation, or 2D (at the
	    listener). bMuffled: heard through walls (low-pass). */
	UAudioComponent* PlayOriginal(const FString& File, const FVector& Location, bool b2D = false, float Volume = 1.f,
		float Pitch = 1.f, bool bMuffled = false);

	/** PlayOriginal from anywhere with a world (gameplay sounds); null on a dedicated server. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Audio", meta = (WorldContext = "WorldContext"))
	static UAudioComponent* Play(const UObject* WorldContext, const FString& File, FVector Location, bool b2D = false,
		float Volume = 1.f, float Pitch = 1.f);

	/** A named 2D loop: started and faded to Volume, faded out at 0; bMuffled low-passes it. */
	void SetLoop2D(FName Key, const FString& File, float Volume, bool bMuffled, float FadeSeconds = 2.f);

	// --- online: the server's sounds (docs/adr/0012 M7; BP_PLAY_WAVE, BP_STOP_WAVE, BP_PLAY_MUSIC)
	/**
	 * Online the server says what plays: the zone's own music, loops and random sounds stop (the
	 * weather's stay). A sound we haven't imported comes from the server's files (the asset cache),
	 * decoded once (Audio/MRServerSound).
	 */
	void SetServerDriven(bool bOn);
	bool IsServerDriven() const { return bServerDriven; }
	/** The room's music (the same track carries on, as the original's MusicPlayFile). */
	void ServerMusic(const FString& File);
	/**
	 * A sound at Location, or 2D. bLoop: until the player leaves the room (proto.h SF_LOOP); Pitch
	 * (SF_RANDOM_PITCH picks one); ObjectId: the object it comes from, to stop it by.
	 */
	void ServerSound(const FString& File, const FVector& Location, bool b2D, bool bLoop, float Pitch, uint32 ObjectId);
	/** BP_STOP_WAVE: that sound, from that object (0: any). */
	void ServerStopSound(const FString& File, uint32 ObjectId);
	/** A new room: its loops end. */
	void ServerStopLoops();
	/** Tests: the server's sounds asked for, and played (imported or decoded). */
	int32 GetServerSoundsAsked() const { return ServerAsked; }
	int32 GetServerSoundsPlayed() const { return ServerPlayed; }
	const FString& GetMusicFile() const { return MusicFile; }

	/** Re-read data/audio and restart the zone's sound. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Audio")
	void Reload();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** A component for an original sound, not yet playing, destroyed when it finishes: 2D or at Location. */
	UAudioComponent* Create(USoundBase* Sound, const FVector& Location, bool b2D);
	int32 ViewZone() const;
	void EnterZone(int32 Zone);
	void SetMusic(const FString& File);
	/** Play this music sound (already found or decoded) as the track File. */
	void SetMusicSound(const FString& File, USoundBase* Sound);
	/** The sound for a server file: imported, else decoded from the server's file (maybe later). bLoop: a looping wave. */
	void ResolveServer(const FString& File, bool bLoop, TFunction<void(USoundBase*)> Done);
	/** Quietly: an imported sound, or null (no warning). */
	USoundBase* FindImported(const FString& File);
	void StartLoops(int32 Zone);
	void StopLoops();
	void TickPeriodic(double Now);
	void ApplySettings();
	USoundAttenuation* Attenuation();
	double Number(const TCHAR* Field, double Default) const;
	TSharedPtr<FJsonObject> Room(int32 Zone) const;

	/** Load every sound the rooms name, asynchronously: playing one never waits on a load. */
	void Preload();

	TSharedPtr<struct FStreamableHandle> Preloaded;
	TSharedPtr<FJsonObject> Rooms;
	TSharedPtr<FJsonObject> Config;
	TMap<FString, TWeakObjectPtr<USoundBase>> Sounds;
	TSet<FString> Missing;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> AttenuationAsset;
	UPROPERTY()
	TObjectPtr<USoundMix> Mix;
	UPROPERTY()
	TMap<FString, TObjectPtr<USoundClass>> Classes;

	int32 Zone = -1;
	FString MusicFile;
	// held strongly: the components belong to no actor, and a weak pointer let the garbage
	// collector take the music mid-track
	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> Loops;
	UPROPERTY()
	TMap<FName, TObjectPtr<UAudioComponent>> NamedLoops;
	double NextMusicCheck = 0.0;
	double NextPeriodic = 0.0;
	double NextSettings = 0.0;
	FString SettingsKey;
	bool bLoopsOn = true;
	bool bRandomOn = true;
	bool bMusicOn = true;

	// online (SetServerDriven)
	bool bServerDriven = false;
	int32 ServerAsked = 0;
	int32 ServerPlayed = 0;
	/** Decoded server sounds by file (lower case), and those being fetched or that failed. */
	TMap<FString, TSharedPtr<const struct FMRPcmSound>> Decoded;
	TMap<FString, TArray<TFunction<void(TSharedPtr<const struct FMRPcmSound>)>>> Waiting;
	TSet<FString> Undecodable;
	/** The server's sounds playing, and their files and objects (to stop them by). */
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> ServerSounds;
	TArray<TPair<FString, uint32>> ServerSoundKeys;
	TArray<bool> ServerSoundLoops;
};
