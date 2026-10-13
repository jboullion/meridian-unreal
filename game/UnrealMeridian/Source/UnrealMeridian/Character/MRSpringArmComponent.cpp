#include "Character/MRSpringArmComponent.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarArmReturnSpeed(TEXT("mr.Camera.ArmReturnSpeed"), 6.f,
		TEXT("How fast the third-person camera moves back out once nothing is in its way (interpolation speed, per second). 0: at once."));
}

FVector UMRSpringArmComponent::BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime)
{
	// the arm's origin this frame (USpringArmComponent::UpdateDesiredArmLocation sets it before the trace)
	const FVector Origin = PreviousArmOrigin;
	const double Full = FVector::Dist(Origin, DesiredArmLocation);
	const double Want = bHitSomething ? FVector::Dist(Origin, TraceHitLocation) : Full;
	const float Speed = CVarArmReturnSpeed.GetValueOnGameThread();
	// a new arm length (the view changed, the wheel zoomed) is taken at once
	if (Length < 0.0 || Want < Length || Speed <= 0.f || !FMath::IsNearlyEqual(LengthFor, TargetArmLength, 1.f))
	{
		Length = Want;
	}
	else
	{
		Length = FMath::FInterpTo(Length, Want, DeltaTime, Speed);
	}
	LengthFor = TargetArmLength;
	return Full > UE_KINDA_SMALL_NUMBER ? Origin + (DesiredArmLocation - Origin) * (Length / Full) : DesiredArmLocation;
}
