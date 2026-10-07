#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRSpriteNetTest.generated.h"

class APlayerController;

/**
 * Sprite sync check (docs/sprites.md), run on two clients of a dedicated server with
 * -MRSpriteNetTest=A and -MRSpriteNetTest=B (tools/ue/run_sprite_net_test.ps1):
 * client A changes its look, colours and height and starts dancing; client B then logs every other
 * character's replicated appearance and action ("MRSpriteNet: saw ..."), then "MRSpriteNet: DONE".
 * Both quit afterwards. Works with -nullrhi: it checks the replicated state, not the drawing.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRSpriteNetTest : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	void Step();

	TWeakObjectPtr<APlayerController> Controller;
	FString Role;
	int32 Ticks = 0;
	FTimerHandle Timer;
};
