#include "Character/MRCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarSpeedScale(TEXT("mr.Move.SpeedScale"), 1.f,
		TEXT("Every speed in the game relative to the original client's (1: walk 6.47 m/s, run 12.94 m/s). ")
		TEXT("The player, other players and monsters; ledge jumps need the original's 1."));
	TAutoConsoleVariable<int32> CVarDebugSteps(TEXT("mr.Move.DebugSteps"), 0,
		TEXT("1: log every step up against a wall (the room's wall found, the original's rule, the floor ahead, the result)."));
	TAutoConsoleVariable<float> CVarStepCap(TEXT("mr.Move.StepCapCm"), 300.f,
		TEXT("The highest step (cm) the original's rule lets you climb at a room's wall. The original has no limit: a wall ")
		TEXT("without a lower texture never blocks, and the desert's cliffs are up to 34 m."));
}

float UMRCharacterMovementComponent::StepCapCm()
{
	return FMath::Max(OriginalStepCm, CVarStepCap.GetValueOnAnyThread());
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

float UMRCharacterMovementComponent::WadingStepHeight() const
{
	// The collision floors of wading sectors are lowered by their sink (roo2gltf SectorHeights), so a
	// pool's bank at the pool's own floor height is a step of 0.88 m (depth 2) or 1.32 m (depth 3). The
	// original never makes you climb it: a wall without a lower texture doesn't block a step at all
	// (clientd3d move.c, blakserv roofile.c BSPCanMoveInRoomTreeInternal), and these banks have none.
	// So in a pool you step up as high as it sinks you; at a room's wall StepUp then applies the
	// original's own rule (which keeps you in the few pools whose level banks have a lower texture).
	const UWorld* World = GetWorld();
	const UMRZoneSubsystem* ZoneSub = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!ZoneSub || !UpdatedComponent)
	{
		return OriginalStepCm;
	}
	// (a server override can stand you above the floor: a negative sink, the original's step)
	const float Sink = ZoneSub->DepthSinkAt(UpdatedComponent->GetComponentLocation());
	return FMath::Max(OriginalStepCm, Sink + 5.f);
}

void UMRCharacterMovementComponent::PhysWalking(float DeltaTime, int32 Iterations)
{
	MaxStepHeight = WadingStepHeight();
	Super::PhysWalking(DeltaTime, Iterations);
}

void UMRCharacterMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	MaxStepHeight = WadingStepHeight();
	Super::PhysFalling(DeltaTime, Iterations);
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

bool UMRCharacterMovementComponent::FloorAhead(const FHitResult& Hit, float Feet, float Limit, float& OutRise, float& OutNormalZ) const
{
	// the lowest floor above our feet just past what we walked into: a trace down from well over Limit (a
	// solid box or prop taller than the step isn't started inside), again under each surface it meets
	// (ceilings face down and are passed from above). A floor higher than Limit is a step too tall.
	const FVector At = UpdatedComponent->GetComponentLocation();
	const FVector2D Dir = -FVector2D(Hit.ImpactNormal).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}
	const FVector2D P = FVector2D(At) + Dir * (FVector2D::Distance(FVector2D(At), FVector2D(Hit.ImpactPoint)) + 5.0);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MRStepAhead), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Query, Response);
	double Top = Feet + Limit + 50.0;
	bool bFound = false;
	for (int32 i = 0; i < 8 && Top > Feet + 1.0; ++i)
	{
		FHitResult Down;
		if (!GetWorld()->LineTraceSingleByChannel(Down, FVector(P, Top), FVector(P, Feet - 5.0), UpdatedComponent->GetCollisionObjectType(), Query, Response))
		{
			break;
		}
		if (Down.ImpactNormal.Z > 0.05 && Down.ImpactPoint.Z > Feet + 1.0)
		{
			OutRise = static_cast<float>(Down.ImpactPoint.Z - Feet);
			OutNormalZ = static_cast<float>(Down.ImpactNormal.Z);
			bFound = true;
		}
		Top = Down.ImpactPoint.Z - 0.5;
	}
	return bFound;
}

void UMRCharacterMovementComponent::LogStepSweeps(const FVector& At, float Feet, const FVector& Delta) const
{
	// what StepUp met: up, forward, down (the same sweeps, not moving)
	const FQuat Q = UpdatedComponent->GetComponentQuat();
	const FCollisionShape Shape = UpdatedPrimitive->GetCollisionShape();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MRStepDebug), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Query, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	FHitResult H;
	const FVector Up = At + FVector(0, 0, MaxStepHeight - CurrentFloor.FloorDist);
	const bool bUpHit = GetWorld()->SweepSingleByChannel(H, At, Up, Q, Channel, Shape, Query, Response);
	const FVector Top = bUpHit ? H.Location : Up;
	UE_LOG(LogTemp, Display, TEXT("MRStep:     up %s (%.1f of %.1f, start penetrating %d)"), bUpHit ? TEXT("hit") : TEXT("clear"),
		Top.Z - At.Z, Up.Z - At.Z, bUpHit && H.bStartPenetrating ? 1 : 0);
	const FVector Fwd = Top + Delta;
	const bool bFwdHit = GetWorld()->SweepSingleByChannel(H, Top, Fwd, Q, Channel, Shape, Query, Response);
	UE_LOG(LogTemp, Display, TEXT("MRStep:     forward %.1f cm: %s (time %.2f, normal %s, at %.1f above the feet, penetrating %d)"), Delta.Size(),
		bFwdHit ? TEXT("hit") : TEXT("clear"), bFwdHit ? H.Time : 1.f, bFwdHit ? *H.ImpactNormal.ToCompactString() : TEXT("-"),
		bFwdHit ? H.ImpactPoint.Z - Feet : 0.0, bFwdHit && H.bStartPenetrating ? 1 : 0);
	const FVector Mid = bFwdHit ? H.Location : Fwd;
	const bool bDownHit = GetWorld()->SweepSingleByChannel(H, Mid, Mid - FVector(0, 0, MaxStepHeight + 4.8f), Q, Channel, Shape, Query, Response);
	UE_LOG(LogTemp, Display, TEXT("MRStep:     down: %s (floor at %.1f above the feet, normal %s, penetrating %d, %.1f cm off the axis)"),
		bDownHit ? TEXT("hit") : TEXT("nothing"), bDownHit ? H.ImpactPoint.Z - Feet : 0.0, bDownHit ? *H.ImpactNormal.ToCompactString() : TEXT("-"),
		bDownHit && H.bStartPenetrating ? 1 : 0, bDownHit ? FVector2D::Distance(FVector2D(H.Location), FVector2D(H.ImpactPoint)) : 0.0);
}

bool UMRCharacterMovementComponent::StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult)
{
	if (FMath::Abs(Hit.ImpactNormal.Z) >= 0.2f)
	{
		return Super::StepUp(GravDir, Delta, Hit, OutStepDownResult);
	}
	const UWorld* World = GetWorld();
	const UMRZoneSubsystem* ZoneSub = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	const FVector At = UpdatedComponent->GetComponentLocation();
	// where we stand: the floor under us (the movement floats a little over it), as the original measures
	const float Feet = At.Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		- (IsMovingOnGround() && CurrentFloor.IsWalkableFloor() ? CurrentFloor.FloorDist : 0.f);
	// a prop or a test box: UE's own step (ours: the original's, or as far as a pool sinks you)
	const float Ours = MaxStepHeight;
	const FMRStepWall* Wall = ZoneSub ? ZoneSub->StepWallAt(Hit.ImpactPoint, At) : nullptr;
	if (!Wall)
	{
		return Super::StepUp(GravDir, Delta, Hit, OutStepDownResult);
	}
	// a room's wall: the original's own rule (clientd3d move.c IntersectNode, blakserv roofile.c). A side
	// without a lower texture never blocks a step; otherwise the higher floor at the wall's first end, less
	// the far sector's sink, may be a step (24 Kod units) above where you stand, and no more. A wall it
	// lets you over may be taller than our step (measured at its low end, or untextured): up to the cap.
	if (Wall->bLowerTexture && Wall->Z1 - Wall->FarSinkCm - Feet > OriginalStepCm + 1.f)
	{
		return false;
	}
	const float Limit = FMath::Max(Ours, StepCapCm());
	float Rise = 0.f, NormalZ = 1.f;
	const bool bAhead = FloorAhead(Hit, Feet, Limit, Rise, NormalZ);
	const bool bDebug = CVarDebugSteps.GetValueOnGameThread() != 0;
	if (bDebug)
	{
		UE_LOG(LogTemp, Display, TEXT("MRStep: %s at (%.0f, %.0f, %.0f) feet %.1f: lower texture %d, z1 %.1f, far sink %.0f, limit %.1f, floor ahead %s %.1f"),
			*GetNameSafe(CharacterOwner), Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z, Feet, Wall->bLowerTexture ? 1 : 0, Wall->Z1,
			Wall->FarSinkCm, Limit, bAhead ? TEXT("yes") : TEXT("no"), Rise);
	}
	if (!bAhead || Rise > Limit)
	{
		return false;
	}
	// Rise just over the floor ahead: UE rises the whole MaxStepHeight before stepping forward, and a
	// low ceiling past the wall stopped that (a step with 2.2 m over it, out of a sewer's water). On a
	// slope the capsule meets the floor up to a radius uphill of its centre: that much higher. The wall
	// begins at our floor.
	const float Slope = NormalZ < 0.999f ? FMath::Min(FMath::Sqrt(1.f - NormalZ * NormalZ) / NormalZ, 3.f) : 0.f;
	const float StepCm = FMath::Min(Rise + 6.f + Slope * UMRCharacterMovementComponent::CapsuleRadiusCm, FMath::Max(Limit, Rise + 6.f));
	TGuardValue<float> Step(MaxStepHeight, StepCm);
	FHitResult Low = Hit;
	Low.ImpactPoint.Z = FMath::Min(Hit.ImpactPoint.Z, static_cast<double>(Feet) + 1.0);
	if (Rise <= Ours + 1.f)
	{
		const bool bOk = Super::StepUp(GravDir, Delta, Low, OutStepDownResult);
		UE_CLOG(bDebug, LogTemp, Display, TEXT("MRStep:   step %.1f: %s"), StepCm, bOk ? TEXT("up") : TEXT("refused"));
		if (bDebug && !bOk)
		{
			LogStepSweeps(At, Feet, Delta);
		}
		return bOk;
	}
	// taller than our own step: onto that floor with the centre a little past the wall, as the original
	// stands you on the floor where you are, or the next floor check (our step) drops you back
	const double ToWall = -FVector2D::DotProduct(FVector2D(At) - Wall->A, Wall->Into);
	const FVector Across(Wall->Into * FMath::Max(ToWall + 5.0, 1.0), 0.0);
	const bool bOk = Super::StepUp(GravDir, Across, Low, OutStepDownResult);
	UE_CLOG(bDebug, LogTemp, Display, TEXT("MRStep:   step %.1f across %.0f cm: %s"), StepCm, Across.Size(), bOk ? TEXT("up") : TEXT("refused"));
	if (bDebug && !bOk)
	{
		LogStepSweeps(At, Feet, Across);
	}
	return bOk;
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
