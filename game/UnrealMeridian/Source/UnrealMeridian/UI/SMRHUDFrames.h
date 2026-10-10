#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class UMRUISubsystem;

/**
 * The HUD over the view, after Meridian Shards' Modern interface (docs/adr/0009-user-interface.md,
 * "The Shards layout"; meridian-browser apps/client/src/game/ui/ModernHud.tsx and the
 * .game.modern-ui rules in its styles.css). Sizes are Shards' CSS pixels from ui_style.json "hud"
 * (UMRUIStyle::HudPx); HUD Size scales each cluster where it stands (SMRHUDRoot's SDPIScalers).
 *   - SMRUnitFrame, top left: our face, name, Rest/Stand and the mailbox, our enchantments
 *   - SMRTargetFrame, top centre: the target's picture and name (red if it can be attacked), and x
 *   - SMRMapCluster, top right: the round map, its - and + buttons, the room's name, the time and
 *     the room's enchantments; a click on the map opens the large map
 *   - SMRActionBar, bottom centre: health and mana, vigor, the spell row and the item hotbar, experience
 * The buttons only take clicks while the cursor is free (a dialog open): the keys do the same.
 */

/** One of the action bar's bars (Shards' .hud-bar): a gradient fill, the limit, and "value / max". */
class UNREALMERIDIAN_API SMRHudBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRHudBar) : _Vital(0), _Height(18.f), _TextSize(11.f), _bText(true) {}
		/** UMRUISubsystem::GetVitalBar: 0 health, 1 mana, 2 vigor, 3 experience. */
		SLATE_ARGUMENT(int32, Vital)
		/** The colour keys in ui_style.json "hud" "colors": <Kind>_top and <Kind>_bottom. */
		SLATE_ARGUMENT(FString, Kind)
		/** In HUD pixels. */
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, TextSize)
		/** Write the value on it (not on the thin experience line). */
		SLATE_ARGUMENT(bool, bText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	int32 Vital = 0;
	FString Kind;
	float Height = 18.f;
	float TextSize = 11.f;
	bool bText = true;
	float Shown = -1.f;  // the fill's fraction, eased toward the value (Shards: width 0.25 s ease-out)
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

/** A small round button in the HUD's panel style (the map's - and +). */
class UNREALMERIDIAN_API SMRRoundButton : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRRoundButton) : _Size(26.f) {}
		SLATE_ARGUMENT(FString, Label)
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
	float Size = 26.f;
	FSimpleDelegate OnClicked;
};

/** Our face in its panel (Shards' .unit-portrait): a click picks us for a waiting spell, a right click looks at us. */
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

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FText Name() const;
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

class UNREALMERIDIAN_API SMRMapCluster : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRMapCluster) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	/** The server's name for the room online, else our zone's. */
	FText RoomName() const;
	/** The Meridian time ("1:00 PM"). */
	FText Clock() const;
};

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
