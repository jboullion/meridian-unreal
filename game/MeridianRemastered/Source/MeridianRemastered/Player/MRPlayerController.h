#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MRPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Feeds the UI keys to UMRUISubsystem (docs/adr/0009-user-interface.md) through its own mapping
 * context, IMC_UI, built in code like the character's: 1-9 and the mouse wheel select the hotbar
 * slot, numpad 1-9 cast from the spell bar, E or I opens the inventory dialog, - and = zoom the map,
 * Enter starts a chat line (playing on a Meridian server).
 *
 * Also drives client-side zone streaming: on a network client, keeps the player's current zone
 * and every zone one exit away loaded and visible (UMRZoneSubsystem::SetClientStreamingTarget),
 * plus any zone the server asks for with ClientPrepareZone (e.g. a teleport to a far zone).
 */
UCLASS()
class MERIDIANREMASTERED_API AMRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMRPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Console: log the current view as a data/environment/lookdev_cameras.json entry. */
	UFUNCTION(Exec)
	void MRBookmark(const FString& Name);

	/** Server -> owning client: start streaming this zone now (it is about to be needed). */
	UFUNCTION(Client, Reliable)
	void ClientPrepareZone(int32 Rid);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void UpdateZoneStreaming();

	// --- UI input (IMC_UI)
	void BuildUIInput();
	void OnHotbarKey(int32 Index);
	void OnHotbarScroll(const FInputActionValue& Value);
	void OnSpellKey(int32 Index);
	void OnInventoryKey();
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
	/** G: pick up what the crosshair is on (several: a list to pick from). */
	void OnGetKey();
	/** F: open the container or work the lever the target or crosshair is on. */
	void OnUseKey();
	/** R: sit down to rest, or stand up (UC_REST, UC_STAND). */
	void OnRestKey();
	/** U: use the selected hotbar item (or the item under the mouse in the dialog) on something. */
	void OnApplyKey();
	class UMRNetWorldSubsystem* GetNetWorld() const;
	void OnMapZoom(const FInputActionValue& Value);
	class UMRUISubsystem* GetUI() const;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> UIContext;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> HotbarActions;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> SpellActions;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HotbarScrollAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> InventoryAction;
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

	/** Zones requested by the server, with the time the request expires. */
	TMap<int32, double> PreparedZones;

	/** Zones recently left, kept resident until the given time. */
	TMap<int32, double> RetainUntil;

	/** Last streaming target sent to the zone subsystem. */
	TSet<int32> StreamingTarget;

	/** Zone the local player was last seen in, and whether its level was visible on entry. */
	int32 LastZone = 0;

	/** Levels requested but not yet visible: zone -> request time, for load-time logging. */
	TMap<int32, double> PendingLoads;

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
	TObjectPtr<class UMRSpriteNetTest> SpriteNetTest;

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
};
