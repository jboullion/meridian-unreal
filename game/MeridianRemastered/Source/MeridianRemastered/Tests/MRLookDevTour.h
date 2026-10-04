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
 *   -MRLookDevLabel=<label>   output folder (default "latest")
 *   -MRLookDevOnly=<a,b>      only these cameras
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

	bool LoadShots();
	void Next();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FShot> Shots;
	FString OutDir;
	int32 Index = -1;
	FTimerHandle Timer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;
};
