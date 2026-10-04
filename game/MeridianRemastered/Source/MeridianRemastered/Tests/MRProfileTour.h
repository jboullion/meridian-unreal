#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRProfileTour.generated.h"

class AMRCharacter;
class APlayerController;

/**
 * Character cost measurement for the base-character spike (docs/adr/0002), run with -MRProfile
 * (add -MRRandomFaces to give every spawned character random head sliders)
 * on a rendering standalone game (usually with -MRStartZone=300 and -MRAppearance=<candidate>).
 *
 * For each crowd size (0, 1, 20, 50 extra characters wearing the current appearance) it spawns
 * the characters in a grid in front of the player, lets them settle, samples frame timings and
 * RHI counters over a few seconds, logs one "MRProfile:" line, saves a screenshot to
 * Saved/Screenshots/MRProfile/, then quits.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRProfileTour : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	void BeginStage();
	void SampleFrame();
	void FinishStage();
	void SpawnUpTo(int32 Count);

	TWeakObjectPtr<APlayerController> Controller;
	TArray<int32> CrowdSizes = {0, 1, 20, 50};
	int32 Stage = -1;

	UPROPERTY()
	TArray<TObjectPtr<AMRCharacter>> Spawned;

	// accumulated samples for the current stage
	int32 Frames = 0;
	double GameMs = 0, RenderMs = 0, GpuMs = 0, Draws = 0, Prims = 0;

	FTimerHandle Timer;
};
