#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRMonsterSubsystem.generated.h"

class AMRMonster;

/**
 * Server: populates the zones with monsters and NPCs the way the original rooms did
 * (docs/sprites.md "Monsters"):
 * - NPCs (shopkeepers, the elder...) stand where the room's Kod places them
 *   (data/zone_layout.json "objects"; their class is in data/monsters.json with AI_NPC).
 * - Monster rooms (data/zones.json "spawning": weighted classes, init_count_min..max at start,
 *   one more every gen_time_ms while under monster_count_max) spawn at the room's generator
 *   points (zone_layout "generators") or, when it has none, at random floor points in the room.
 *
 * Console: mr.Monster.Spawn 0 disables it (before the map starts); MRMonsterReset respawns all.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRMonsterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMRMonsterSubsystem, STATGROUP_Tickables); }

	/** Spawn one monster of a class at a floor point (server). Null if it couldn't be placed. */
	AMRMonster* SpawnMonster(FName Class, int32 Zone, const FVector& Near, float Yaw, bool bRandomise = true);

	/** Kill every monster and spawn the rooms again. */
	void Reset();

	int32 NumAlive() const;

private:
	struct FSpawnRoom
	{
		int32 Rid = 0;
		TArray<TPair<FName, float>> Classes;  // weighted
		TArray<FVector> Generators;           // world, on the floor
		int32 InitMin = 0, InitMax = 0, Max = 0;
		float GenSeconds = 20.f;
		float Timer = 0.f;
		TArray<TWeakObjectPtr<AMRMonster>> Alive;
	};
	TArray<FSpawnRoom> Rooms;
	bool bStarted = false;
	float StartDelay = 2.f;

	void Start();
	void SpawnInRoom(FSpawnRoom& Room);
	void SpawnNpcs(const TSharedPtr<class FJsonObject>& Layout);
	bool FindFloor(int32 Rid, const FVector& Near, float Jitter, float HalfHeight, float Radius, FVector& Out) const;
};
