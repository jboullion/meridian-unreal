#include "Character/MRCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarSpeedScale(TEXT("mr.Move.SpeedScale"), 1.f,
		TEXT("Every speed in the game relative to the original client's (1: walk 6.47 m/s, run 12.94 m/s). ")
		TEXT("The player, other players and monsters; ledge jumps need the original's 1."));
}

float UMRCharacterMovementComponent::SpeedScale()
{
	return FMath::Max(0.05f, CVarSpeedScale.GetValueOnAnyThread());
}

UMRCharacterMovementComponent::UMRCharacterMovementComponent()
{
	MaxWalkSpeed = OriginalRunCms;
	// the original has no crouching or jumping
	NavAgentProps.bCanCrouch = false;
	NavAgentProps.bCanJump = false;
	// the original's step: 24 Kod units, also while falling (HandleImpact)
	MaxStepHeight = OriginalStepCm;
	// Off a ledge, the original keeps its full height until its centre leaves the floor, and stops 53 cm
	// short of a wall (move.c min_distance); our capsule's radius is 34. A round capsule bottom rolled
	// off the edge first and lost about 20 cm (the 3.9 m ledge jump a coin toss), so the base is flat;
	// and the pawn may stand on an edge only within 19 cm of its centre (53 - 34), so a jump covers the
	// original's distance: about 4.1 m running, 2.3 m walking.
	bUseFlatBaseForFloorChecks = true;
	PerchRadiusThreshold = 15.f;
	// short steps, so the fall starts within a few cm of that point at any frame rate (a 60 fps frame
	// runs 22 cm, more than the walking jump's margin)
	MaxSimulationTimeStep = 1.f / 120.f;
	MaxSimulationIterations = 16;
	// the original's gravity (UE's default is 980 cm/s/s)
	GravityScale = OriginalGravityCms2 / 980.f;
	// the original moves at full speed at once, stops at once, and steers fully while falling
	MaxAcceleration = 12000.f;
	BrakingDecelerationWalking = 12000.f;
	BrakingDecelerationFalling = 12000.f;
	FallingLateralFriction = GroundFriction;
	AirControl = 1.f;
	AirControlBoostMultiplier = 1.f;
}

float UMRCharacterMovementComponent::WadingFactor() const
{
	const UWorld* World = GetWorld();
	const UMRZoneSubsystem* ZoneSub = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!ZoneSub || !UpdatedComponent)
	{
		return 1.f;
	}
	return UMRZoneSubsystem::DepthSpeedFactor(ZoneSub->DepthAt(UpdatedComponent->GetComponentLocation()));
}

float UMRCharacterMovementComponent::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking || MovementMode == MOVE_Falling)
	{
		const float Speed = bWantsToWalk ? WalkCms() : RunCms();
		// wading through a field or pool, as the original (clientd3d move.c UserMovePlayer);
		// falling keeps the full speed, as the original moves you across a gap at your own pace
		return MovementMode == MOVE_Falling ? Speed : Speed * WadingFactor();
	}
	return Super::GetMaxSpeed();
}

void UMRCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	// The original steps up to MAX_STEP_HEIGHT from where you are even while falling (move.c: the
	// step is measured from max(floor, your current height)); that is what makes its ledge jumps.
	// UE never steps up while falling (CanStepUp and StepUp refuse in MOVE_Falling), so try the
	// step as if walking, against the wall just hit, and walk on from the top if it works.
	if (IsFalling() && !bSteppingUpInAir && FMath::Abs(Hit.ImpactNormal.Z) < 0.2f)
	{
		const FVector Across(MoveDelta.X, MoveDelta.Y, 0.f);
		if (!Across.IsNearlyZero())
		{
			TGuardValue<bool> Guard(bSteppingUpInAir, true);
			const double FromZ = UpdatedComponent->GetComponentLocation().Z;
			FScopedMovementUpdate ScopedStep(UpdatedComponent, EScopedUpdate::DeferredUpdates);
			FStepDownResult StepDown;
			bool bStepped = false;
			{
				TGuardValue<TEnumAsByte<EMovementMode>> AsWalking(MovementMode, MOVE_Walking);
				// onto the far rim the original steps as soon as it reaches it: no perch limit there
				TGuardValue<float> AnyPerch(PerchRadiusThreshold, 0.f);
				bStepped = StepUp(GetGravityDirection(), Across * (1.f - Hit.Time), Hit, &StepDown);
			}
			// only a step that climbed onto a floor: a "step" that ends no higher (onto nothing, or down
			// to a floor far below) made the pawn walk in mid-air and hang against the wall
			if (bStepped && !(StepDown.bComputedFloor && StepDown.FloorResult.IsWalkableFloor()
				&& UpdatedComponent->GetComponentLocation().Z > FromZ + 1.0))
			{
				ScopedStep.RevertMove();
				bStepped = false;
			}
			if (bStepped)
			{
				Velocity.Z = 0.f;
				SetMovementMode(MOVE_Walking);  // PhysFalling stops when the mode changes
				return;
			}
		}
	}
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
}

void UMRCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// off a ledge: the original starts the fall at FALL_VELOCITY_0, not at rest
	if (MovementMode == MOVE_Falling && PreviousMovementMode == MOVE_Walking && Velocity.Z > -OriginalFallStartCms)
	{
		Velocity.Z = -OriginalFallStartCms;
	}
}

void UMRCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UMRCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UMRCharacterMovementComponent* MutableThis = const_cast<UMRCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_MR(*this);
	}
	return ClientPredictionData;
}

// ------------------------------------------------------------------------------- saved move

void FSavedMove_MR::Clear()
{
	Super::Clear();
	bSavedWantsToWalk = 0;
}

uint8 FSavedMove_MR::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToWalk)
	{
		Result |= FLAG_Custom_1;
	}
	return Result;
}

bool FSavedMove_MR::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_MR* Other = static_cast<const FSavedMove_MR*>(NewMove.Get());
	if (bSavedWantsToWalk != Other->bSavedWantsToWalk)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_MR::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UMRCharacterMovementComponent* Move = Cast<UMRCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedWantsToWalk = Move->bWantsToWalk;
	}
}

void FSavedMove_MR::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UMRCharacterMovementComponent* Move = Cast<UMRCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bWantsToWalk = bSavedWantsToWalk;
	}
}
