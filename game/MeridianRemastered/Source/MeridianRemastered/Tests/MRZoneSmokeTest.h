#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRZoneSmokeTest.generated.h"

class APawn;

/**
 * Server-side end-to-end check of zone travel, run when the server is started with -MRZoneTest.
 * The first player to spawn is moved onto real exit squares and edges from the original data,
 * and each step logs PASS/FAIL ("MRZoneTest:" lines), then "MRZoneTest: DONE n/m passed".
 */
UCLASS()
class MERIDIANREMASTERED_API UMRZoneSmokeTest : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APawn* InPawn);

private:
	struct FStep
	{
		FString Label;
		int32 PlaceZone = 0;
		int32 Row = 0;
		int32 Col = 0;
		int32 ExpectZone = 0;
		bool bNeedsFloor = true;
		/** Use UMRZoneSubsystem::TeleportPawn (waits for the client to stream) instead of placing. */
		bool bServerTeleport = false;
		/** Seconds to wait before the step (e.g. so the client unloads zones it left). */
		float PreDelay = 0.f;
		/**
		 * A wading square (a field, a pool): the original's depth there, or 0 to skip. The pawn must
		 * stand at the wading floor (not on the raised wheat), and move slower (DepthSpeedFactor).
		 */
		int32 ExpectDepth = 0;
		/** Press "go" (the space bar) after placing: doors need it. False checks a door stays shut. */
		bool bGo = true;
	};

	void RunStep();
	void CheckStep();

	TWeakObjectPtr<APawn> Pawn;
	TArray<FStep> Steps;
	int32 Index = 0;
	double TeleportStart = 0.0;
	int32 Passed = 0;
	FTimerHandle Timer;
};
