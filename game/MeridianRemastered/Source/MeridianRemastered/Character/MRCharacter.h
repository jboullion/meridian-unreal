#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Character/MRSpriteAppearance.h"
#include "MRCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UMRCharacterMovementComponent;
class UMRAttributeSet;
class UMRSpriteBodyComponent;
struct FInputActionValue;

/** Camera views (docs/sprites.md Phase 3). V cycles them. */
UENUM(BlueprintType)
enum class EMRViewMode : uint8
{
	FirstPerson,  // the eyes (like the original)
	Chase,        // third person, orbits with the mouse, the body turns toward movement
	Behind,       // fixed behind the character; the mouse turns the character
	Front,        // fixed in front, looking back at the character (selfies); the mouse turns the character
};

/**
 * The player character.
 *
 * - Views (V cycles, the mouse wheel zooms): first person (default, like the original), chase
 *   (third person; the body turns toward movement), and fixed cameras behind and in front of the
 *   character (the mouse turns the character). Third-person cameras stay near eye level: they
 *   orbit freely but tilt only mr.Camera.ThirdPersonPitch (20) degrees up or down; first person
 *   looks up and down freely. P takes a photo without
 *   the HUD into Saved/Screenshots/Photos.
 * - Input actions and the mapping context are created in code, so there are no binary input
 *   assets to keep in sync.
 * - Gaits: run (default), walk (Caps Lock), sprint (Shift, drains Vigor). They are predicted
 *   in UMRCharacterMovementComponent.
 * - The Ability System Component lives on AMRPlayerState.
 * - Drawn like the original game: composited directional sprites (UMRSpriteBodyComponent,
 *   docs/sprites.md). The look, creator colours and height replicate (FMRSpriteAppearance); a
 *   test client picks its own with -MRSpriteLook=<name>. Test keys: LMB attack, 1 wave, 2 point,
 *   3 dance, 4 cast, L next look. The ACharacter skeletal mesh is unused and hidden.
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
	bool IsFirstPerson() const { return ViewMode == EMRViewMode::FirstPerson; }

	/** First person, or the chase camera. */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetFirstPerson(bool bNewFirstPerson);

	UFUNCTION(BlueprintPure, Category = "Camera")
	EMRViewMode GetViewMode() const { return ViewMode; }

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetViewMode(EMRViewMode NewMode);

	/** Save a screenshot without the HUD (P): Saved/Screenshots/Photos/photo_<time>.png. */
	void TakePhoto();

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

	/** Fixed views (behind / in front): distance and starting pitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FixedArmLength = 260.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FixedPitch = 0.f;

	/** Sprite look (data/sprites/player_parts.json) for new characters (DefaultGame.ini). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	FName DefaultSpriteLook = TEXT("test_male");

	/** The sprite drawing; null on a dedicated server or a -nullrhi client (nothing is drawn there). */
	UMRSpriteBodyComponent* GetSpriteBody() const { return SpriteBody; }

	/**
	 * How this character looks as a sprite (look, creator colours, height), replicated: set it on
	 * the owning client (predicted, sent to the server) or the server; every client draws it.
	 */
	void SetSpriteAppearance(const FMRSpriteAppearance& NewAppearance);
	const FMRSpriteAppearance& GetSpriteAppearance() const { return SpriteAppearance; }

	/** Play a sprite action (an attack, an emote; None = stop) here and on every other client. */
	void PlaySpriteAction(FName Action);
	FName GetReplicatedSpriteAction() const { return SpriteAction.Action; }

	/** Sprite height variety (docs/sprites.md Phase 6): scales the drawing and the eyes, not the capsule. */
	void SetSpriteHeight(float Scale);

	/** What ended up on screen: "sprite:<look>". */
	const FString& GetAppearanceDescription() const { return AppearanceDescription; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> SpriteBody;
	bool bAppliedCommandLineAppearance = false;

	UPROPERTY(ReplicatedUsing = OnRep_SpriteAppearance)
	FMRSpriteAppearance SpriteAppearance;

	UPROPERTY(ReplicatedUsing = OnRep_SpriteAction)
	FMRSpriteActionState SpriteAction;

	UFUNCTION()
	void OnRep_SpriteAppearance();
	UFUNCTION()
	void OnRep_SpriteAction();
	UFUNCTION(Server, Reliable)
	void ServerSetSpriteAppearance(const FMRSpriteAppearance& NewAppearance);
	UFUNCTION(Server, Reliable)
	void ServerPlaySpriteAction(FName Action);

	/** Draw SpriteAppearance (look, colours, height) on this machine's sprite body. */
	void ApplySpriteAppearance();
	/** -MRSpriteLook= / -MRSpriteHeight= / -MRSpriteColours=skin,hair,shirt,pants for this machine's own character. */
	void ApplyCommandLineAppearance();
	void ApplySpriteHeight(float Scale);
	/** Create the sprite body (clients that render) and draw Look on it. */
	void CreateSpriteBody(FName Look);
	void ApplyFirstPersonVisibility();

	FString AppearanceDescription;

	/** First-person camera position in actor space: the original's eye height, scaled with the sprite's height. */
	FVector FirstPersonEye = FVector(12.f, 0.f, 75.f);

	/** Replicated because the rotation rules depend on it (the controller's yaw turns the body in
	 * first person and the fixed views; the chase camera turns it toward movement). */
	UPROPERTY(ReplicatedUsing = OnRep_ViewMode)
	EMRViewMode ViewMode = EMRViewMode::FirstPerson;

	/** The fixed views' camera tilt and distance (local). */
	float FixedViewPitch = -10.f;
	float FixedViewArm = 260.f;

	UFUNCTION()
	void OnRep_ViewMode();

	UFUNCTION(Server, Reliable)
	void ServerSetViewMode(EMRViewMode NewMode);

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
	void OnAttack();
	void OnEmote(FName Action);
	void OnNextLook();
	void OnPhoto();
	/** Keep the third-person camera within mr.Camera.ThirdPersonPitch of eye level. */
	void ClampThirdPersonPitch();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DefaultContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WalkAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ViewAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> AttackAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> NextLookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> PhotoAction;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> EmoteActions;

	bool bWalkToggled = false;
	float ZoneCheckAccumulator = 0.f;
};
