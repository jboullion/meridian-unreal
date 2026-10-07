#include "Character/MRCharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/MRAttributeSet.h"
#include "GameFramework/Character.h"
#include "Zones/MRZoneSubsystem.h"

UMRCharacterMovementComponent::UMRCharacterMovementComponent()
{
	MaxWalkSpeed = RunSpeed;
	NavAgentProps.bCanCrouch = true;
	// The original allowed 24 Kod units (~0.8 m) steps; a modern character uses stairs and ramps.
	MaxStepHeight = 45.f;
	JumpZVelocity = 420.f;
	AirControl = 0.2f;
}

bool UMRCharacterMovementComponent::HasVigorToSprint() const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!ASC)
	{
		return true; // no ability system (e.g. a test pawn): don't block sprinting
	}
	bool bFound = false;
	const float Vigor = ASC->GetGameplayAttributeValue(UMRAttributeSet::GetVigorAttribute(), bFound);
	return !bFound || Vigor > 1.f;
}

bool UMRCharacterMovementComponent::IsSprinting() const
{
	return bWantsToSprint && !bWantsToWalk && IsMovingOnGround() && !IsCrouching()
		&& Velocity.SizeSquared2D() > FMath::Square(RunSpeed * 0.5f) && HasVigorToSprint();
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
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		float Speed = RunSpeed;
		if (IsCrouching())
		{
			Speed = MaxWalkSpeedCrouched;
		}
		else if (bWantsToWalk)
		{
			Speed = WalkSpeed;
		}
		else if (bWantsToSprint && HasVigorToSprint())
		{
			Speed = SprintSpeed;
		}
		// wading through a field or pool, as the original (clientd3d move.c UserMovePlayer)
		return Speed * WadingFactor();
	}
	return Super::GetMaxSpeed();
}

void UMRCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
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
	bSavedWantsToSprint = 0;
	bSavedWantsToWalk = 0;
}

uint8 FSavedMove_MR::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}
	if (bSavedWantsToWalk)
	{
		Result |= FLAG_Custom_1;
	}
	return Result;
}

bool FSavedMove_MR::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_MR* Other = static_cast<const FSavedMove_MR*>(NewMove.Get());
	if (bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToWalk != Other->bSavedWantsToWalk)
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
		bSavedWantsToSprint = Move->bWantsToSprint;
		bSavedWantsToWalk = Move->bWantsToWalk;
	}
}

void FSavedMove_MR::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UMRCharacterMovementComponent* Move = Cast<UMRCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bWantsToSprint = bSavedWantsToSprint;
		Move->bWantsToWalk = bSavedWantsToWalk;
	}
}
