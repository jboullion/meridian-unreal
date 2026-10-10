#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "MRHitchTour.generated.h"

class APlayerController;

/**
 * Hitch measurement, run with -MRHitchTour on an offline game (tools/ue/run_hitch_test.ps1). Unlike
 * the look-dev profile (still cameras, slowed world clock) this plays at normal speed: from the first
 * frame it records every frame's time, waits for the warm-up screen (UMRWarmup) like a player, then
 * takes the player through Raza, the Outskirts and a few interiors, turning a full circle and walking a
 * little at each stop. Writes Saved/MRHitch/<label>.csv (one row per frame) and logs
 * "MRHitch: ..." summary lines, then quits. -MRHitchLabel=<name> names the CSV; -MRHitchCsv also records a
 * CSV profile with per-pass GPU timings (Saved/Profiling/CSV/).
 */
UCLASS()
class UNREALMERIDIAN_API UMRHitchTour : public UObject, public FTickableGameObject
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
	struct FStop
	{
		FString Name;
		FVector Eye;  // world cm; zero: stay where the player is
		float Yaw = 0.f;
	};
	struct FFrame
	{
		double Time;
		float FrameMs, GameMs, RenderMs, GpuMs;
		int32 Stop;
	};

	void BeginStop();
	void Finish();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FStop> Stops;
	TArray<FFrame> Frames;
	FString Label;
	double StartTime = 0.0;
	double LastFrameTime = 0.0;
	double StopStart = 0.0;
	double WarmupSeconds = -1.0;
	int32 StopIndex = -1;  // -1: waiting for the warm-up
	bool bRunning = false;
	bool bCsv = false;
};
