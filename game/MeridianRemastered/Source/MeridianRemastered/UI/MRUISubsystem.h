#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/MRUITypes.h"
#include "MRUISubsystem.generated.h"

class AMRAvatarPreview;
class APlayerController;
class IToolTip;
class SMRHUDRoot;
class SWidget;
class UMRAttributeSet;
class UMRGameDataSubsystem;
class UMRInventorySource;
class UMRUIStyle;
struct FSlateBrush;

/**
 * The in-game UI for one local player (docs/adr/0009-user-interface.md): the HUD (vitals, item
 * hotbar, spell bar, minimap) and the inventory dialog (Inventory, Spells, Skills, Stats, Quests),
 * drawn with Slate in C++ over the original client's art.
 *
 * AMRHUD shows it for a local player that can draw (never on a dedicated server or -nullrhi);
 * AMRPlayerController feeds it the UI keys (1-9 and the wheel select the hotbar, numpad 1-9 cast,
 * E / I open the dialog, - / = zoom the map).
 */
UCLASS()
class MERIDIANREMASTERED_API UMRUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Create the HUD in the viewport for this controller (once). */
	void ShowHUD(APlayerController* PC);
	void RemoveHUD();
	bool HasHUD() const { return HUD.IsValid(); }

	// --- keys (from AMRPlayerController)
	void OnHotbarKey(int32 Index);
	void OnHotbarScroll(float Delta);
	void OnSpellKey(int32 Index);
	void OnMapZoom(float Steps);
	void ToggleInventory();
	void SetInventoryOpen(bool bOpen);
	/** Show a page of the dialog (EMRInventoryTab order: inventory, spells, skills, stats, quests). */
	void SetInventoryTab(int32 Tab);
	bool IsInventoryOpen() const { return bInventoryOpen; }

	// --- slots (from the slot widgets)
	void OnSlotMouseDown(const FMRSlotRef& Slot, bool bRight, bool bShift);
	void OnSlotMouseUp(const FMRSlotRef& Slot);
	void SetHoveredSlot(const FMRSlotRef& Slot, bool bHovered);
	const FMRSlotRef& GetHoveredSlot() const { return HoveredSlot; }
	/** The mouse's last position as the UI's events saw it (absolute): the carried stack is drawn there. */
	void NoteMouse(const FVector2D& ScreenSpace) { MouseScreen = ScreenSpace; bHasMouse = true; }
	bool GetMouse(FVector2D& OutScreenSpace) const { OutScreenSpace = MouseScreen; return bHasMouse; }
	/** Clicked outside the dialog window. */
	void OnClickOutside(bool bRight);

	// --- what the widgets draw
	UMRInventorySource* GetSource() const { return Source; }
	UMRUIStyle* GetStyle() const;
	UMRGameDataSubsystem* GetData() const;
	UMRAttributeSet* GetAttributes() const;
	APlayerController* GetPlayerController() const;
	const FSlateBrush* IconFor(const FMRSlotContent& Content) const;
	FText NameFor(const FMRSlotContent& Content) const;
	TSharedPtr<IToolTip> MakeToolTip(const FMRSlotContent& Content);
	/** Spell bar cooldown left (0..1) after a cast. */
	float SpellCooldown(int32 Index) const;
	/** Seconds since the spell bar was last used (it shows fully for a moment). */
	double SpellBarLastUsed() const { return LastSpellKeyTime; }
	/** Time of the last hotbar selection change (the item name fades out after it). */
	double LastSelectionTime() const { return SelectionTime; }
	float GetMapZoom() const { return MapZoom; }
	/** The avatar render target (null until the dialog has been opened). */
	UObject* GetAvatarTarget() const;
	/** Turn the avatar by a number of the original's eight angles. */
	void TurnAvatar(int32 Steps);
	double Now() const;
	/** Where each slot was last drawn (absolute / desktop pixels): -MRUIShots points the mouse at one. */
	void NoteSlotDrawn(const FMRSlotRef& Slot, FVector2f AbsoluteCentre) { SlotCentres.Add(SlotKey(Slot), AbsoluteCentre); }
	bool GetSlotCentre(const FMRSlotRef& Slot, FVector2f& OutAbsolute) const;
	/** The minimap picture of a geometry zone (kept loaded), or null if it hasn't been captured. */
	class UTexture2D* GetMapTexture(int32 GeometryRid);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMRInventorySource> Source;

	UPROPERTY(Transient)
	TObjectPtr<AMRAvatarPreview> Avatar;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<class UTexture2D>> MapTextures;

	TWeakObjectPtr<APlayerController> OwnerPC;
	TSharedPtr<SMRHUDRoot> HUD;
	bool bInventoryOpen = false;
	FMRSlotRef HoveredSlot;
	/** Where the mouse went down with an empty cursor: releasing over another slot places (drag and drop). */
	FMRSlotRef PressSlot;
	bool bPickedOnPress = false;
	double SpellCastTime[9] = {};
	double LastSpellKeyTime = -100.0;
	double SelectionTime = -100.0;
	float MapZoom = 1.f;

	TMap<uint32, FVector2f> SlotCentres;
	FVector2D MouseScreen = FVector2D::ZeroVector;
	bool bHasMouse = false;
	static uint32 SlotKey(const FMRSlotRef& Slot) { return (static_cast<uint32>(Slot.Area) << 16) | static_cast<uint32>(Slot.Index & 0xFFFF); }

	void ApplyInputMode();
	void EnsureAvatar();
	void OnStyleReloaded();
	FDelegateHandle StyleHandle;
};
