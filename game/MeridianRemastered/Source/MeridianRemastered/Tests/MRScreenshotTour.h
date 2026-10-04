#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRScreenshotTour.generated.h"

class APlayerController;

/**
 * Visual check run with -MRScreenshots on a rendering client (standalone or connected):
 * once the local character exists, takes a few screenshots from fixed viewpoints
 * (third person, first person ahead, first person looking down at the body, mid-swing attack
 * montages) into Saved/Screenshots/MRTour/, then quits. Combine with -MRStartZone=300 for daylight in Raza.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRScreenshotTour : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	struct FShot
	{
		FString Name;
		bool bFirstPerson = false;
		float Pitch = 0.f;
		float YawOffset = 0.f;
		FString Montage;          // played just before the capture, if set
		float CaptureDelay = 1.5f; // seconds after positioning (or starting the montage)
	};

	void Next();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FShot> Shots;
	int32 Index = -1;
	FTimerHandle Timer;
};
