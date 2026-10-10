#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MRPlayerController.generated.h"

enum class EMRWindow : uint8;

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Feeds the UI keys to UMRUISubsystem (docs/adr/0009-user-interface.md) through its own mapping
 * context, IMC_UI, built in code like the character's: 1-9 and the mouse wheel select the hotbar
 * slot, numpad 1-9 cast from the spell bar, E or I opens the inventory dialog, - and = zoom the map,
 * Enter starts a chat line (playing on a Meridian server).
 */
UCLASS()
class UNREALMERIDIAN_API AMRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Map the keys again if the player changed them (Options > Controls): ours and the pawn's. */
	void RemapKeysIfChanged();
	AMRPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Console: log the current view as a data/environment/lookdev_cameras.json entry. */
	UFUNCTION(Exec)
	void MRBookmark(const FString& Name);


protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:

	// --- UI input (IMC_UI)
	void BuildUIInput();
	void OnHotbarKey(int32 Index);
	void OnHotbarScroll(const FInputActionValue& Value);
	void OnSpellKey(int32 Index);
	void OnInventoryKey();
	/** O: who is on; L: mail; Y: the guild (online). */
	void OnWindowKey(EMRWindow Window);
	/** F1-F12 online: the quick chat's line (the original's function-key aliases). */
	void OnQuickChatKey(int32 Index);
	void MapUIKeys();
	int32 MappedKeyVersion = 0;
	void OnChatKey();
	/** Esc or F10: the Escape menu (in PIE, Esc stops play: use F10 there). */
	void OnMenuKey();
	/** Tab or ] (Shift+Tab: back), [ , \ (self), T (what the crosshair is on): the original's targets (merintr.c). */
	void OnTargetNext();
	void OnTargetPrevious();
	void OnTargetSelf();
	void OnTargetAim();
	/** Right mouse button: look at the target, else what the crosshair is on (the original's look-mouse). */
	void OnLookKey();
	/** The look key let go: with the free cursor, a right click that didn't turn the view looks. */
	void OnLookReleased();
	/** Look at the target, else what the crosshair (or cursor) is on; bCursorFirst: what the cursor is on first. */
	void LookAtAim(bool bCursorFirst);
	bool bLookPending = false;
	double LookPressTime = 0.0;
	/** G: pick up what the crosshair is on (several: a list to pick from). */
	void OnGetKey();
	/** F: open the container or work the lever the target or crosshair is on. */
	void OnUseKey();
	/** R: sit down to rest, or stand up (UC_REST, UC_STAND). */
	void OnRestKey();
	/** U: use the selected hotbar item (or the item under the mouse in the dialog) on something. */
	void OnApplyKey();
	/** H: the HUD away for pictures, and back (Shards' Hide Interface). */
	void OnHideInterfaceKey();
	class UMRNetWorldSubsystem* GetNetWorld() const;
	void OnMapZoom(const FInputActionValue& Value);
	class UMRUISubsystem* GetUI() const;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> UIContext;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> HotbarActions;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> SpellActions;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HotbarScrollAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InventoryAction;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> QuickChatActions;
	UPROPERTY(Transient) TObjectPtr<UInputAction> WhoAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MailAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> GuildAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MapWindowAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ChatAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MapZoomAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> TargetNextAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> TargetPreviousAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> TargetSelfAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> TargetAimAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> GetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> UseAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RestAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ApplyAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HideInterfaceAction;

	/** -MRScreenshots visual check. */
	UPROPERTY()
	TObjectPtr<class UMRScreenshotTour> ScreenshotTour;

	/** -MRProfile character cost measurement. */
	UPROPERTY()
	TObjectPtr<class UMRProfileTour> ProfileTour;

	/** -MRLookDev environment captures. */
	UPROPERTY()
	TObjectPtr<class UMRLookDevTour> LookDevTour;

	UPROPERTY()
	TObjectPtr<class UMRSpriteClipTour> SpriteClipTour;

	UPROPERTY()
	TObjectPtr<class UMRMonsterTour> MonsterTour;

	/** -MRMapCapture minimap pictures. */
	UPROPERTY()
	TObjectPtr<class UMRMapCapture> MapCapture;

	/** -MRUIShots UI screenshots. */
	UPROPERTY()
	TObjectPtr<class UMRUIShots> UIShots;

	/** -MRNetTest: plays on a Meridian server. */
	UPROPERTY()
	TObjectPtr<class UMRNetTest> NetTest;

	/** -MRMoveTest: the original's movement (speeds, ledge jumps, steps), checked in play. */
	UPROPERTY(Transient)
	TObjectPtr<class UMRMoveTest> MoveTest;

	/** -MRStepSurvey: every step the original lets you take, tried with the player's capsule and movement. */
	UPROPERTY(Transient)
	TObjectPtr<class UMRStepSurvey> StepSurvey;
};
