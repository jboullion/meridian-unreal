#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "MRMoveTest.generated.h"

class AActor;
class APlayerController;

/**
 * The original's movement, checked in play (docs/findings.md "Walking"): run with -MRMoveTest on an
 * offline game (tools/ue/run_move_test.ps1). The player's own pawn, driven by movement input:
 * - runs and walks on a flat test floor and measures the speeds;
 * - runs off a ledge across a 2.5 m gap onto a platform as high as the ledge (the original's ledge
 *   "jump": you step up mid-fall), near the original's limit of about 4.1 m (3.9 m lands, 4.5 m doesn't), and
 *   across 5 m; walking across 2.5 m falls in;
 * - runs up one of the Mausoleum's 0.72 m steps (taller than a modern step, within the original's);
 * - wades at half speed at depth 2, walks out of a pool onto a bank level with its floor (1.32 m and
 *   0.88 m up from where the pool sinks you), and not onto a bank 0.99 m up;
 * - runs and walks up a 0.825 m step (the original's most), stops at 0.85 m, walks under a beam 1.66 m
 *   up (the original's player is 1.65 m tall); and steps between the sectors of a room built at runtime,
 *   up a 1.51 m one without a lower texture (the original never blocks there), not a 3.99 m one (the cap),
 *   and through a one-way wall from its passable side only.
 * Logs "MRMoveTest: PASS|FAIL ..." per step and "MRMoveTest: DONE n/m", then quits.
 */
UCLASS()
class UNREALMERIDIAN_API UMRMoveTest : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bRunning; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

private:
	struct FStep
	{
		FString Label;
		/** Place the pawn and build what the step needs. */
		TFunction<void()> Setup;
		/** Movement input direction while the step runs. */
		FVector Direction = FVector::ForwardVector;
		bool bWalk = false;
		float Seconds = 2.f;
		/** -> pass, with what to log. */
		TFunction<bool(FString&)> Check;
	};

	void Begin(int32 NewIndex);
	void Finish();
	/** A box of collision (and a drawn cube) from Min to Max, world cm. */
	AActor* Box(const FVector& Min, const FVector& Max);
	/** The boxes, and the test pool's wading area. */
	void ClearBoxes();
	/**
	 * A room built at runtime (AMRRuntimeRoom) at the rig: two sectors, the east one StepKod higher from the
	 * rig's x on; bOneWay: a textured wall between them that only the west side's sidedef lets you through.
	 */
	void RuntimeStep(int16 StepKod, bool bLowerTexture, bool bOneWay);
	/** A wading area (UMRZoneSubsystem::SetTestDepthArea) over Area, world XY; Depth 0: none. */
	void SetTestPool(const FBox2D& Area, int32 Depth);
	void Place(const FVector& FeetAt, const FVector& Facing);
	double FeetZ() const;

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FStep> Steps;
	TArray<TWeakObjectPtr<AActor>> Boxes;
	int32 Index = -1;
	double StepTime = 0.0;
	bool bRunning = false;
	int32 Passed = 0;
	/** Speed sampling: position and time at the start of the measured window. */
	FVector SampleFrom = FVector::ZeroVector;
	double SampleStart = -1.0;
	double MeasuredSpeed = 0.0;
	/** The step's first fall: where it left the ground and where it was next on the ground (cm past the rig; feet, cm). */
	FString Flight;
	double TakeOffX = 0.0;
	bool bWasFalling = false;
};
