#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MRCharacterMovementComponent.generated.h"

/**
 * Character movement with client-predicted run / walk gaits: run by default, walk while Shift is
 * held, as the original's two speeds (no sprint, no crouch, no jump: TODO 2026-10-07).
 * The gait travels in the saved-move compressed flags, so the server replays the client's
 * moves at the same speed instead of correcting them.
 * Wading through the original's fields and pools slows every gait (UMRZoneSubsystem::DepthAt): the
 * depth comes from the position, so client and server agree without sending it.
 *
 * Moves like the original client (clientd3d move.c, moveobj.c; docs/findings.md "Walking"), which
 * is what its ledge "jumps" depend on (run off a ledge, step onto the far side before you've
 * dropped more than a step):
 * - speed: a quarter square per 85 ms walking, twice that running (6.47 / 12.94 m/s at 2.2 m a
 *   square), times mr.Move.SpeedScale; full speed at once, and full steering while falling;
 * - falling: gravity 5 squares/s/s (11 m/s/s), starting at 2/3 square/s (1.47 m/s) down;
 * - steps up to 24 Kod units (0.825 m), also while falling (HandleImpact), when the step climbs onto a floor;
 * - off a ledge: a flat base keeps its height, and the fall starts 19 cm past the edge (the original's
 *   53 cm reach less our 34 cm radius), so its jumps go as far: about 4.1 m running, 2.3 m walking.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_MR;

public:
	UMRCharacterMovementComponent();

	/** The original's walk: MOVEUNITS (1/4 square) per MOVE_DELAY (85 ms), cm/s (before mr.Move.SpeedScale). */
	static constexpr float OriginalWalkCms = 1024.f / 4.f * (220.f / 1024.f) / 0.085f;  // 647
	static constexpr float OriginalRunCms = 2.f * OriginalWalkCms;                       // 1294
	/** The original's falling (moveobj.h GRAVITY_ACCELERATION, move.h FALL_VELOCITY_0), cm/s/s and cm/s. */
	static constexpr float OriginalGravityCms2 = 5.f * 220.f;                           // 1100
	static constexpr float OriginalFallStartCms = 2.f / 3.f * 220.f;                     // 147
	/** The original's MAX_STEP_HEIGHT: 24 Kod units, cm. */
	static constexpr float OriginalStepCm = 24.f / 64.f * 220.f;                         // 82.5

	/** mr.Move.SpeedScale: every speed in the game relative to the original's (1 = the original). */
	static float SpeedScale();
	/** The player's walk and run, cm/s, with mr.Move.SpeedScale: other characters' speeds follow these. */
	static float WalkCms() { return OriginalWalkCms * SpeedScale(); }
	static float RunCms() { return OriginalRunCms * SpeedScale(); }

	void SetWantsToWalk(bool bWalk) { bWantsToWalk = bWalk; }
	bool WantsToWalk() const { return bWantsToWalk; }

	virtual float GetMaxSpeed() const override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f, const FVector& MoveDelta = FVector::ZeroVector) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	bool bWantsToWalk = false;
	bool bSteppingUpInAir = false;

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
