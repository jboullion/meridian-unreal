#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "MRCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UMRCharacterMovementComponent;
class UMRAttributeSet;
struct FInputActionValue;

/**
 * The player character.
 *
 * - First person (default, like the original) or third person, toggled with V or the mouse wheel.
 *   First person uses the controller's yaw; third person turns the body toward movement.
 * - Input actions and the mapping context are created in code, so there are no binary input
 *   assets to keep in sync.
 * - Gaits: run (default), walk (Caps Lock), sprint (Shift, drains Vigor). They are predicted
 *   in UMRCharacterMovementComponent.
 * - The Ability System Component lives on AMRPlayerState.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMRCharacter(const FObjectInitializer& ObjectInitializer);

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMRAttributeSet* GetAttributeSet() const;
	UMRCharacterMovementComponent* GetMRMovement() const;

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Camera")
	bool IsFirstPerson() const { return bFirstPerson; }

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetFirstPerson(bool bNewFirstPerson);

	/** Vigor drained per second while sprinting. */
	UPROPERTY(EditDefaultsOnly, Category = "Vigor")
	float SprintVigorPerSecond = 12.f;

	/**
	 * Placeholder Vigor regeneration while not sprinting. The original regenerates through
	 * exertion and resting (player.kod NewVigor / ExertionTimer); that port replaces this.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Vigor")
	float VigorRegenPerSecond = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float ThirdPersonArmLength = 320.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float MaxArmLength = 600.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Stand-in body until the MetaHuman is in. Hidden from its owner in first person. */
	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(ReplicatedUsing = OnRep_FirstPerson)
	bool bFirstPerson = true;

	UFUNCTION()
	void OnRep_FirstPerson();

	UFUNCTION(Server, Reliable)
	void ServerSetFirstPerson(bool bNewFirstPerson);

	void ApplyViewMode();
	void InitAbilityActorInfo();
	void ServerTickVigor(float DeltaSeconds);
	void ServerTickZone(float DeltaSeconds);

	// --- input
	void BuildInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnSprintStarted();
	void OnSprintStopped();
	void OnToggleWalk();
	void OnToggleView();
	void OnZoom(const FInputActionValue& Value);
	void OnCrouchToggle();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DefaultContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WalkAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ViewAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;

	bool bWalkToggled = false;
	float ZoneCheckAccumulator = 0.f;
};
