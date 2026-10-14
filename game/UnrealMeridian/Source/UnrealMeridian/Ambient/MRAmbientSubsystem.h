#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ambient/MRAmbientLife.h"
#include "Ambient/MRCritters.h"
#include "Ambient/MRArenaPlayback.h"
#include "MRAmbientSubsystem.generated.h"

class AMRAmbientFigure;
class AMRNatureActor;
class FMRSocialClient;
class FJsonObject;

/**
 * Ambient life in the room the player is in (docs/adr/0013-ambient-life.md): Meridian Shards' townsfolk,
 * inn patrons, courtiers and shop customers (FMRAmbientLife), drawn as AMRAmbientFigure sprites. Only this
 * client sees them; the server never hears of them.
 *
 * - The room: online the server's (its file and our zone for it), offline the zone the player is in.
 * - Its crowd (data/ambient/ambient.json), nav grid and doors: data/ambient/ (the rooms we built), else the
 *   server's asset cache (nav/<room>.nav, roomexits.json), as Shards' own asset build wrote them.
 * - Each frame: where the player stands and looks, the game hour, the weather, fine armour, and the real
 *   objects in the way (online those that block, MOVEON_NO; offline the NPCs).
 * - mr.Ambient.Level: 0 off, 1 few, 2 normal (default), 3 many (Options > Game > Ambient life).
 */
UCLASS()
class UNREALMERIDIAN_API UMRAmbientSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	/** The setting's population scale: 0 off, 0.5 few, 1 normal, 2 many. */
	static double LevelScale();

	/**
	 * An attack or a click with nothing real there: the critter under the cursor ray, else the nearest
	 * ahead within reach, is squished (docs/adr/0013). True if one was.
	 */
	bool SquishCritter(const FVector& RayStart, const FVector& RayDir);
	int32 NumCritters() const { return Critters.IsValid() ? Critters->NumAlive() : 0; }
	const FMRCritters* GetCritters() const { return Critters.Get(); }
	const AMRNatureActor* GetNature() const { return Nature.Get(); }
	/** The player waved (our wave emote, online UA_WAVE): the nearest in front waves back. */
	void OnPlayerWave();
	/** A spell went off at World (its target or where it landed): those near run. */
	void OnSpell(const FVector& World, double RadiusCm = 4.0 * 220.0);

	/** Someone of the crowd speaking: where (world, over their head) and what. */
	struct FSpeech
	{
		FVector Anchor;
		FString Text;
	};
	TArray<FSpeech> GetSpeech() const;
	/** The figure the crosshair or cursor ray hits nearest, within MaxCm of it (0: none). */
	uint32 FigureNear(const FVector& RayStart, const FVector& RayDir, double MaxDistCm = 3000.0) const;
	bool Describe(uint32 Id, FMRAmbientLife::FPersonInfo& Out) const;
	/** Examine on someone: their name, the inspect window's text and their sprite look; false if nobody. */
	bool Inspect(uint32 Id, FString& OutName, FString& OutText, FMRSpriteAppearance& OutLook) const;
	/** The crowd's figures (tests). */
	const TMap<uint32, TObjectPtr<AMRAmbientFigure>>& GetFigures() const { return Figures; }
	/** The room the crowd is for ("razainn.roo"), or empty (tests). */
	const FString& GetRoomFile() const { return RoomFile; }
	bool HasCrowd() const { return Life.IsValid(); }
	/** The arena's playback while in it (tests) */
	const FMRArenaPlayback* GetArena() const { return Arena.Get(); }

private:
	int32 Rid = 0;
	FString RoomFile;
	/** Waiting for the asset cache's nav grid or doors */
	bool bLoading = false;
	TUniquePtr<FMRAmbientLife> Life;
	TUniquePtr<FMRCritters> Critters;
	/** Butterflies, fish, birds and bats (one actor for all) */
	UPROPERTY(Transient)
	TObjectPtr<AMRNatureActor> Nature;
	/** The room's terrain and whether bats live there ("covered": caves and dungeons, "necropolis": crypts, prisons) */
	TArray<FString> Terrain;
	FString BatsKind;
	double SkyAge = 10.0;
	bool bSky = false;
	void StepNature(float DeltaTime, double Scale);
	/** The arena's fights from Meridian Shards' relay (one connection, kept from room to room) */
	TSharedPtr<FMRSocialClient> Social;
	FString SocialFor;
	TUniquePtr<FMRArenaPlayback> Arena;
	void StartArena();
	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<AMRAmbientFigure>> Figures;
	struct FShown
	{
		int32 PoseSerial = -1;
		bool bTorch = false;
		FVector Feet = FVector::ZeroVector;
		FVector2D Roo = FVector2D(-1.0, -1.0);
		double FloorAge = 0.0;
	};
	TMap<uint32, FShown> Shown;
	/** Each visit's crowd from its own seed (-MRAmbientSeed=N: the same crowd every run, for tests) */
	uint32 Visits = 0;
	/** The doors of every room (roomexits.json), once read */
	TSharedPtr<FJsonObject> ExitsJson;
	bool bExitsFromCache = false;

	void Reset();
	/** The room the player is in now: its zone and file */
	bool CurrentRoom(int32& OutRid, FString& OutFile) const;
	void StartRoom(int32 InRid, const FString& InFile);
	void Begin(const FMRNavGrid& Grid, const FMRAmbientLife::FExits& Exits);
	bool ReadExits(const TSharedPtr<FJsonObject>& Json, const FString& File, FMRAmbientLife::FExits& Out) const;
	FMRAmbientLife::FFrame MakeFrame() const;
	void Sync(float DeltaTime);
	FVector FloorAt(const FVector2D& Roo, FShown& S, bool bForce) const;
};
