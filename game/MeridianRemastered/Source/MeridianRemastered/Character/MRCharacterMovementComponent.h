#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MRCharacterMovementComponent.generated.h"

/**
 * Character movement with client-predicted walk / run / sprint gaits.
 * The gait travels in the saved-move compressed flags, so the server replays the client's
 * moves at the same speed instead of correcting them.
 * Sprinting needs Vigor; the character stops sprinting when its replicated Vigor runs out.
 * Wading through the original's fields and pools slows every gait (UMRZoneSubsystem::DepthAt): the
 * depth comes from the position, so client and server agree without sending it.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_MR;

public:
	UMRCharacterMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gait")
	float WalkSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gait")
	float RunSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gait")
	float SprintSpeed = 700.f;

	void SetWantsToSprint(bool bSprint) { bWantsToSprint = bSprint; }
	void SetWantsToWalk(bool bWalk) { bWantsToWalk = bWalk; }
	bool WantsToSprint() const { return bWantsToSprint; }

	/** True while actually sprinting: wants to, has vigor, on the ground and moving. */
	UFUNCTION(BlueprintPure, Category = "Gait")
	bool IsSprinting() const;

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	bool bWantsToSprint = false;
	bool bWantsToWalk = false;

	bool HasVigorToSprint() const;

	/** The original's wading slowdown where the character stands: 1, or 3/4, 1/2, 1/4 (UMRZoneSubsystem). */
	float WadingFactor() const;
};

class FSavedMove_MR : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;

	uint8 bSavedWantsToSprint : 1 = 0;
	uint8 bSavedWantsToWalk : 1 = 0;
};

class FNetworkPredictionData_Client_MR : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_MR(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement) {}

	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSavedMove_MR()); }
};
