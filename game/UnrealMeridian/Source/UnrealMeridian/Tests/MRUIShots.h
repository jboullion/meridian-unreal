#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRUIShots.generated.h"

class APlayerController;

/**
 * The UI's visual check (docs/adr/0009-user-interface.md), run with -MRUIShots on a rendering
 * game (tools/ue/run_ui_shots.ps1): screenshots *with* the UI of the HUD (a hotbar selection, low
 * health, the spell bar after a cast), the inventory dialog on every tab, a carried stack and a
 * tooltip, into Saved/Screenshots/MRUI/<-MRUIShotsLabel>/, then quits.
 */
UCLASS()
class UNREALMERIDIAN_API UMRUIShots : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	struct FStep
	{
		FString Name;  // empty: no screenshot
		TFunction<void(APlayerController*)> Do;
		float Delay = 1.f;
	};

	void Next();
	/** Point the mouse at a slot (its last drawn centre). */
	static void PointAt(APlayerController* PC, int32 Area, int32 Index);

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FStep> Steps;
	int32 Index = -1;
	FString Dir;
	FTimerHandle Timer;
};
