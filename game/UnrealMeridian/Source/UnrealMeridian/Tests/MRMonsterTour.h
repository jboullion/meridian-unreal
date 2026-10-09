#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRMonsterTour.generated.h"

class APlayerController;
class AMRMonster;

/**
 * Monster line-up (docs/sprites.md "Monsters"), run with -MRMonsters on a standalone game:
 * every monster class in a row in front of the player and every NPC in a row behind it, AI off;
 * screenshots of them standing, walking, attacking and dead (Saved/Screenshots/MRMonsters/),
 * first from the front, then from the side. Then a chase: an aggressive mummy and a provoked
 * bunny with their AI on, logged (distance, attacks) and photographed after a few seconds; then quits.
 *
 * In a zone with a spawn table (-MRStartZone=306, 330...) it photographs the monsters the spawner put
 * there instead: the player is moved next to up to four of them in turn (zone_1.png...), then it quits.
 */
UCLASS()
class UNREALMERIDIAN_API UMRMonsterTour : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	void Next();
	void Spawn();
	void Shot(const FString& Name, float Delay);
	/** The next spawner-made monster of the start zone to photograph, the player moved next to it; false when done. */
	bool NextZoneShot(class AMRCharacter* Player);

	TWeakObjectPtr<APlayerController> Controller;
	UPROPERTY() TArray<TObjectPtr<AMRMonster>> Spawned;
	UPROPERTY() TArray<TObjectPtr<AMRMonster>> Chasers;
	int32 Step = 0;
	int32 ZoneShots = 0;
	bool bZoneMode = false;
	FTimerHandle Timer;
};
