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
class UMRCharacterAppearance;
class USceneComponent;
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
 * - Looks come from a UMRCharacterAppearance (a MakeHuman body on the mannequin skeleton, plus
 *   parts). Fallbacks: configured appearance -> plain engine mannequin -> placeholder cylinder.
 */
UCLASS(Config = Game)
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

	/**
	 * First-person eyes relative to the driver's head bone (reference pose, actor space: X forward,
	 * Z up). The head bone sits at the top of the neck; eyes are a little above and in front.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector EyeOffsetFromHeadBone = FVector(10.f, 0.f, 9.f);

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

	/** Appearance for player characters (DefaultGame.ini). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	FSoftObjectPath DefaultAppearance;

	/** Apply an appearance (null = fallbacks). Safe to call again to change looks. */
	void ApplyAppearance(const UMRCharacterAppearance* Appearance);

	/**
	 * Set the head sliders (values in [-1, 1], in the appearance's HeadSliders order; missing
	 * values are 0). Applied as morph-target weights on the body and on every part (hair follows
	 * the head shape because it carries the same morphs).
	 */
	void ApplyHeadSliders(const TArray<float>& Values);

	/** Random slider values for testing crowds (deterministic per seed). */
	void ApplyRandomHeadSliders(int32 Seed, float Strength = 0.6f);

	/** What ended up on screen: "appearance:<name>", "mannequin" or "placeholder". */
	const FString& GetAppearanceDescription() const { return AppearanceDescription; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Last-resort body when no character or mannequin content is installed. */
	UPROPERTY(VisibleAnywhere, Category = "Body")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Components created from the appearance's parts, by part name. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USceneComponent>> AppearanceParts;

	/** Parts hidden from the owner in first person. */
	TSet<FName> FirstPersonHiddenParts;
	FName FirstPersonBodyPart;
	FName DriverHiddenBone;
	FName BodyPartHiddenBone;
	FString AppearanceDescription = TEXT("placeholder");

	void ClearAppearance();

	/** The appearance currently applied (for its sliders). */
	UPROPERTY(Transient)
	TObjectPtr<const UMRCharacterAppearance> CurrentAppearance;
	USceneComponent* CreateAppearancePart(const struct FMRAppearancePart& Part, USceneComponent* Parent);
	bool ApplyMannequinFallback();
	void ApplyFirstPersonVisibility();
	/** Place the first-person eyes from the driver skeleton (so taller bodies see from higher up). */
	void UpdateEyePosition();

	/** First-person camera position in actor space (from UpdateEyePosition). */
	FVector FirstPersonEye = FVector(12.f, 0.f, 75.f);

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
