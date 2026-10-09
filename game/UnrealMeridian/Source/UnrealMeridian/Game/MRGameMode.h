#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MRGameMode.generated.h"


/**
 * Server rules. Players start in their home zone (the Raza Inn for new characters, as in
 * settings.kod piInitialHomeRoomID) at that zone's original teleport point.
 */
UCLASS()
class UNREALMERIDIAN_API AMRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMRGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/**
	 * Playing on a Meridian server (UMRNetSubsystem::WantsOnline): the player gets a pawn only when
	 * the server puts their character in a room (UMRNetWorldSubsystem), at the server's position.
	 */
	void SpawnOnlinePlayer(AController* Player, const FTransform& At);

	/** Zone new characters start in. Override with -MRStartZone=<rid> (e.g. 300, the town square). */
	UPROPERTY(EditDefaultsOnly, Category = "Zones")
	int32 StartZone = 301; // RID_RAZA_INN

private:
	/** Players waiting for their client to stream the start zone before spawning. */
	TMap<TWeakObjectPtr<AController>, double> SpawnWaitStart;
};
