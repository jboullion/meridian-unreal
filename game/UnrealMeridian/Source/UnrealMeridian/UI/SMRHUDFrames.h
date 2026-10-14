#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SScrollBox;
class SVerticalBox;
class UMRUISubsystem;

/**
 * The HUD over the view, after Meridian Shards' Modern interface (docs/adr/0009-user-interface.md,
 * "The Shards layout" and its second pass; meridian-browser apps/client/src/game/ui/ModernHud.tsx and
 * the .game.modern-ui rules in its styles.css). Sizes are Shards' CSS pixels from ui_style.json "hud"
 * (UMRUIStyle::HudPx); HUD Size scales each cluster where it stands (SMRHUDRoot's SDPIScalers).
 *   - SMRUnitFrame, top left: our face, beside it health, mana and vigor (SMRVitalBars), our
 *     enchantments under them; a click on the face opens the quick emotes
 *   - SMRTargetFrame, top centre: the target's picture and name (red if it can be attacked), and x
 *   - SMRQuickEmotes, top centre under it: wave, point, dance and the moods (Q)
 *   - SMRMapCluster, top right: the round map, Rest/Stand and the mailbox at its top corners, its -
 *     and + at the bottom, the room's name and enchantments; a click on the map opens the large map
 *   - SMRActionBar, bottom centre: the quick slots, a row of ten (1-0) with the numpad's above it
 *   - SMRQuickSlotPicker: a quick slot's spell or item, chosen from a list
 * The buttons only take clicks while the cursor is free: the keys do the same.
 */

/**
 * Health, mana and vigor beside our face (Shards' Sidebar.tsx HudBars, .hud-bars): each the stat's
 * icon and the original's graph bar (graphctl.c: a green fill, the dark red limit, the dark red
 * background, the number in white, in the original's bar frame), in a faint dark box. A click picks
 * us while a spell waits for its target.
 */
class UNREALMERIDIAN_API SMRVitalBars : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRVitalBars) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	/** Each bar's fill, eased toward its value (Shards: width 0.25 s ease-out); -1 until the first. */
	float Shown[3] = {-1.f, -1.f, -1.f};
};

/** A toolbar button from the original's art (toolbar.c: the bitmap's left half up, right half down). */
class UNREALMERIDIAN_API SMRToolButton : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRToolButton) {}
		/** The art pieces <Piece>_up and <Piece>_down (tools/ui/build_ui_art.py). */
		SLATE_ARGUMENT(FName, Piece)
		/** A toggle's state: drawn pressed while true (Rest/Stand while resting). */
		SLATE_ATTRIBUTE(bool, bPressed)
		SLATE_ARGUMENT(FText, ToolTip)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FName Piece;
	TAttribute<bool> bPressed;
	FSimpleDelegate OnClicked;
	bool bDown = false;
};

/**
 * A small round button in the HUD's panel style: the map's - and + (a label), and Rest/Stand and the
 * mailbox at its top corners (Shards' .map-tool: the toolbar's art in a disc, a gold edge and glow
 * while pressed).
 */
class UNREALMERIDIAN_API SMRRoundButton : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRRoundButton) : _Size(26.f) {}
		SLATE_ARGUMENT(FString, Label)
		/** The toolbar art drawn in it instead of a label (<Piece>_up / _down; the up half). */
		SLATE_ARGUMENT(FName, Piece)
		/** A toggle's state: a gold edge and glow while true (resting). */
		SLATE_ATTRIBUTE(bool, bPressed)
		/** In HUD pixels. */
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(FText, ToolTip)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FString Label;
	FName Piece;
	TAttribute<bool> bPressed;
	float Size = 26.f;
	FSimpleDelegate OnClicked;
};

/**
 * Our face in its panel (Shards' .unit-portrait): a click picks us for a waiting spell, else opens
 * the quick emotes; a right click looks at us.
 */
class UNREALMERIDIAN_API SMRPortrait : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRPortrait) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FSlateBrush Face;
};

class UNREALMERIDIAN_API SMRUnitFrame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRUnitFrame) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** The parts the Help's HUD page points at. */
	TSharedPtr<SWidget> GetFace() const { return Face; }
	TSharedPtr<SWidget> GetBars() const { return Bars; }
	TSharedPtr<SWidget> GetEnchantments() const { return Enchantments; }

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SWidget> Face, Bars, Enchantments;
};

class UNREALMERIDIAN_API SMRTargetFrame : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRTargetFrame) {}
		/** The widest it may be, in Slate units (Shards: 40 % of the view). */
		SLATE_ATTRIBUTE(float, MaxWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	/** The target, if there is one to show. */
	const struct FMRNetObject* Target() const;
	bool IsAttackable(const struct FMRNetObject& O) const;
	FVector2f ClearButtonPos(const FVector2f& Size) const;
	TWeakObjectPtr<UMRUISubsystem> UI;
	TAttribute<float> MaxWidth;
};

/**
 * The quick emotes (Shards' GameView.tsx QUICK_EMOTES, .quick-emotes): Wave, Point, Dance | Happy,
 * Sad, Neutral, Wry, in a row of buttons; one is done and the row closes (UMRUISubsystem::RunEmote).
 */
class UNREALMERIDIAN_API SMRQuickEmotes : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRQuickEmotes) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	/** Each button's box (and a zero box for the gap), in local Slate units. */
	void Layout(TArray<FBox2f>& Out) const;
	int32 ButtonAt(const FVector2f& Local) const;
	TWeakObjectPtr<UMRUISubsystem> UI;
	int32 Hovered = INDEX_NONE;
};

class UNREALMERIDIAN_API SMRMapCluster : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRMapCluster) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** The parts the Help's HUD page points at. */
	TSharedPtr<SWidget> GetMap() const { return Map; }
	TSharedPtr<SWidget> GetTools() const { return Tools; }
	TSharedPtr<SWidget> GetRoomName() const { return RoomLabel; }
	TSharedPtr<SWidget> GetEnchantments() const { return Enchantments; }

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SWidget> Map, Tools, RoomLabel, Enchantments;
	/** The server's name for the room online, else our zone's. */
	FText RoomName() const;
};

/** The quick slots (Shards' ActionBar, QuickSlots.tsx Hotbar): ten a row, the numpad's row over the first. */
class UNREALMERIDIAN_API SMRActionBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRActionBar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};

/**
 * What goes on a quick slot (Shards' QuickSlotPicker): the spells we know, then the items we carry (one
 * row a kind, shillings left out), and Empty; the slot's current choice marked. Over the view, in the
 * HUD's panel; a click picks and closes it, Esc or a click outside closes it.
 */
class UNREALMERIDIAN_API SMRQuickSlotPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRQuickSlotPicker) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	void Rebuild();
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SWidget> Panel;
	/** The slot the list was made for (-1: none): made again each time the picker opens. */
	int32 BuiltFor = INDEX_NONE;
};

/** A short note in the middle of the view that fades (Shards' .view-note): "Interface hidden (H)". */
class UNREALMERIDIAN_API SMRViewNote : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRViewNote) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};
