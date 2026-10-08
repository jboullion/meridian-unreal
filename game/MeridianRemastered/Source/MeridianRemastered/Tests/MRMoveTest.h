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
 * - runs up one of the Mausoleum's 0.72 m steps (taller than a modern step, within the original's).
 * Logs "MRMoveTest: PASS|FAIL ..." per step and "MRMoveTest: DONE n/m", then quits.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRMoveTest : public UObject, public FTickableGameObject
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
	void ClearBoxes();
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
};
