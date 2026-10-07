#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MRHUD.generated.h"

/**
 * The in-game HUD. Shows the Slate UI (UMRUISubsystem: vitals, hotbars, minimap, inventory
 * dialog; docs/adr/0009-user-interface.md) for its local player, and draws the original game's first-person hand or weapon (a
 * "window overlay", docs/sprites.md Phase 3) when the player is a sprite body in first person:
 * anchored to the bottom-right corner (HS_SE) and scaled like the original client
 * (clientd3d/overlay.c ComputePlayerOverlayArea: viewport width / 452 * 0.5), bobbing while walking.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void DrawHUD() override;

private:
	float BobPhase = 0.f;
	double LastTime = 0.0;
};
