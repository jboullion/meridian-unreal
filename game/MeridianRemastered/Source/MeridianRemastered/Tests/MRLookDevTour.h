#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRLookDevTour.generated.h"

class ACameraActor;
class APlayerController;

/**
 * Environment look-dev captures (docs/adr/0003), run with -MRLookDev on a rendering standalone game
 * (usually through tools/ue/run_lookdev.ps1):
 * renders every camera bookmark in data/environment/lookdev_cameras.json from a fixed camera with the
 * player hidden, into Saved/Screenshots/MRLookDev/<label>/<camera>.png, then quits.
 *
 * Each camera is saved as soon as its image has settled (Lumen, shadows and exposure converge after
 * a camera cut): the frame is sampled every SampleSeconds and kept once a coarse brightness grid stops
 * changing, instead of waiting a fixed time. World time is slowed to a crawl meanwhile, so clouds and
 * wind hold still and captures don't depend on how long each camera took.
 *
 *   -MRLookDevLabel=<label>   output folder (default "latest")
 *   -MRLookDevOnly=<a,b>      only these cameras
 *   -MRLookDevSettle=<s>      wait a fixed <s> seconds per camera instead (the old behaviour)
 *   -MRLookDevProfile         also profile every camera: for each variant (everything on, Nanite
 *                             tessellation off, grass hidden) settle, then average CPU/GPU frame
 *                             times over SampleFrames frames ("MRLookDevProfile:" log lines) and
 *                             write a CSV-profiler capture with per-pass GPU timings next to the
 *                             screenshots (<camera>__<variant>.csv; tools/lookdev/profile_report.py).
 *
 * The `MRBookmark <name>` console command (AMRPlayerController) logs the current view as a
 * bookmark entry to paste into the camera file.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRLookDevTour : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

	/** One bookmark as a lookdev_cameras.json entry for the given view. */
	static FString FormatBookmark(const FString& Name, const FVector& Location, const FRotator& Rotation, float Fov);

private:
	struct FShot
	{
		FString Name;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Fov = 90.f;
	};

	struct FVariant
	{
		FString Name;
		TArray<FString> Commands;
	};

	bool LoadShots();
	void Next();
	void Sample();
	void OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void Captured();
	/** Audio mode (-MRLookDevAudio=<seconds>): record the main mix at the shot, then go on. */
	void StopRecording();
	void After(float Seconds, void (UMRLookDevTour::*Step)());
	void NextVariant();
	void StartSampling();
	void SampleFrame();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FShot> Shots;
	FString OutDir;
	int32 Index = -1;

	float FixedSettle = 0.f;      // > 0: -MRLookDevSettle
	double ShotStart = 0.0;       // real time of the camera cut
	TArray<TArray<float>> Grids;  // recent brightness grids of this shot, oldest first
	int32 StableCount = 0;
	bool bExposureCut = false;    // the second cut of this shot is done
	FDelegateHandle FrameHandle;

	bool bProfile = false;
	float AudioSeconds = 0.f;     // > 0: -MRLookDevAudio, a WAV per shot
	bool bWithPawn = false;       // -MRLookDevWithPawn: the player's character at the shot, seen through its own camera
	TArray<FVariant> Variants;
	int32 VariantIndex = -1;
	int32 Frames = 0;
	double GameMs = 0, RenderMs = 0, GpuMs = 0;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;
};
