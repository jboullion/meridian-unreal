#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "MRSpringArmComponent.generated.h"

/**
 * The third-person camera's arm. When something comes between the camera and the player it pulls in at once,
 * as the engine's does (never through a wall); when the way clears it eases back out over a moment
 * (mr.Camera.ArmReturnSpeed) instead of jumping, which in the original's narrow rooms popped back and forth.
 */
UCLASS()
class UNREALMERIDIAN_API UMRSpringArmComponent : public USpringArmComponent
{
	GENERATED_BODY()

protected:
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime) override;

private:
	/** The arm's length drawn last frame, cm (negative: none yet), and the target it was for. */
	double Length = -1.0;
	float LengthFor = -1.f;
};
