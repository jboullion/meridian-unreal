#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MRGameMode.generated.h"

class UMRZoneSmokeTest;

/**
 * Server rules. Players start in their home zone (the Raza Inn for new characters, as in
 * settings.kod piInitialHomeRoomID) at that zone's original teleport point.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMRGameMode();

	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Zone new characters start in. */
	UPROPERTY(EditDefaultsOnly, Category = "Zones")
	int32 StartZone = 301; // RID_RAZA_INN

private:
	/** Players waiting for their client to stream the start zone before spawning. */
	TMap<TWeakObjectPtr<AController>, double> SpawnWaitStart;

	/** Created when the server runs with -MRZoneTest. */
	UPROPERTY()
	TObjectPtr<UMRZoneSmokeTest> ZoneSmokeTest;
};
