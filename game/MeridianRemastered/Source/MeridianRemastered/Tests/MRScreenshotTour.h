#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRScreenshotTour.generated.h"

class APlayerController;

/**
 * Visual check run with -MRScreenshots on a rendering client (standalone or connected):
 * once the local character exists, takes a few screenshots from fixed viewpoints
 * (third person, first person ahead and looking down, mid-swing sprite attacks, eight shots
 * orbiting the character every 45 degrees as sprite_angle_<deg>) into Saved/Screenshots/MRTour/,
 * then quits. Combine with -MRStartZone=300 for daylight in Raza. view_behind / view_front use
 * the fixed cameras.
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
		bool bAttack = false;      // play the sprite's attack just before the capture
		float CaptureDelay = 1.5f; // seconds after positioning (or starting the attack)
		int32 View = -1;           // an EMRViewMode to use instead of bFirstPerson (fixed cameras)
	};

	void Next();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FShot> Shots;
	int32 Index = -1;
	FTimerHandle Timer;
};
