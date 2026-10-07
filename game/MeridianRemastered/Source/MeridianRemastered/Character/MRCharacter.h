#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Character/MRSpriteAppearance.h"
#include "MRCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UMRCharacterMovementComponent;
class UMRAttributeSet;
class UMRCharacterAppearance;
class UMRSpriteBodyComponent;
class USceneComponent;
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
 * - Looks come from a UMRCharacterAppearance (a MakeHuman body on the mannequin skeleton, plus
 *   parts). Fallbacks: configured appearance -> plain engine mannequin -> placeholder cylinder.
 * - Sprite body experiment (docs/sprites.md): with -MRSpriteBody, mr.Character.SpriteBody 1 or
 *   bSpriteBody in DefaultGame.ini the character is drawn like the original game instead
 *   (UMRSpriteBodyComponent, look from -MRSpriteLook=<name>). Test keys: LMB attack, 1 wave,
 *   2 point, 3 dance, 4 cast, L next look.
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

	/** Fixed views (behind / in front): distance and starting pitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FixedArmLength = 260.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float FixedPitch = 0.f;

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

	/** Draw this character as an original-style sprite (DefaultGame.ini; see the class comment). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	bool bSpriteBody = false;

	/** Sprite look (data/sprites/player_parts.json) when drawn as a sprite. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	FName DefaultSpriteLook = TEXT("test_male");

	/** True if this character is drawn as a sprite (decided at BeginPlay). */
	bool UsesSpriteBody() const { return bUseSpriteBody; }
	UMRSpriteBodyComponent* GetSpriteBody() const { return SpriteBody; }

	/** Switch to the sprite body with a look (also from tests and crowds). */
	void ApplySpriteBody(FName Look);

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

	/** What ended up on screen: "appearance:<name>", "sprite:<look>", "mannequin" or "placeholder". */
	const FString& GetAppearanceDescription() const { return AppearanceDescription; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> SpriteBody;
	bool bUseSpriteBody = false;
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
