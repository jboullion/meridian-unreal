#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRSpriteClipTour.generated.h"

class APlayerController;

/**
 * Motion capture for the sprite smoothing tests (docs/sprites.md Phase 4), run with
 * -MRSpriteClip=<walk|action name> [-MRClipFrames=60] on a fixed time step
 * (-UseFixedTimeStep -FPS=30, so every run sees the same game time per frame):
 * the chase camera looks at the character from the side; "walk" walks it across the view, any
 * other name plays that action (dance, wave, fist_attack, weapon_attack...). One screenshot per
 * frame into Saved/Screenshots/MRClip/, then quits. tools/sprites/clip_gif.py makes GIFs.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRSpriteClipTour : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	void Setup();
	void Frame();

	TWeakObjectPtr<APlayerController> Controller;
	FString Clip;
	int32 Frames = 60;
	int32 Index = 0;
	float CameraYaw = 0.f;
	FTimerHandle Timer;
};
