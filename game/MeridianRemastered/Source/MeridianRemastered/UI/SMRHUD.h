#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SMRInventoryScreen;
class SMRMinimap;
class UMRUISubsystem;

/**
 * A stone panel (docs/adr/0009-user-interface.md): a tiled background piece, a frame of the
 * original's corner strips and repeaters, optional gargoyle corners (the original 3D view's), and
 * the content inside the frame.
 */
class MERIDIANREMASTERED_API SMRPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRPanel) : _Background(TEXT("bkgnd")), _Frame(TEXT("edge")), _bCorners(false), _Padding(4.f), _BackgroundTint(FLinearColor::White) {}
		SLATE_ARGUMENT(FName, Background)
		SLATE_ARGUMENT(FName, Frame)
		SLATE_ARGUMENT(bool, bCorners)
		/** Extra padding inside the frame, in original pixels. */
		SLATE_ARGUMENT(float, Padding)
		SLATE_ARGUMENT(FLinearColor, BackgroundTint)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

	/** Width of the transparent band around a cornered panel. */
	static float CornerBand(class UMRUIStyle* Style);

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FName Background;
	FName Frame;
	bool bCorners = false;
	FLinearColor BackgroundTint;
};

/** A stat bar like the original's (graphctl.c: a coloured fill, the gold caps), with its value. */
class MERIDIANREMASTERED_API SMRBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRBar) : _Width(90.f), _Height(9.f), _bShowText(true) {}
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ATTRIBUTE(float, Max)
		SLATE_ARGUMENT(FLinearColor, Color)
		/** In original pixels. */
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(bool, bShowText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	TAttribute<float> Value;
	TAttribute<float> Max;
	FLinearColor Color;
	float Width = 90.f;
	float Height = 9.f;
	bool bShowText = true;
	float Shown = -1.f;      // displayed fraction, eased toward the value
	float Trail = -1.f;      // a lighter trail that follows losses (damage)
	float FlashTime = 0.f;
};

/** What the mouse carries: drawn at the cursor, on top of everything. */
class MERIDIANREMASTERED_API SMRCursorStack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRCursorStack) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};

/** The selected item's name, above the hotbar for a moment after the selection changes (as Minecraft). */
class MERIDIANREMASTERED_API SMRItemName : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRItemName) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};

/**
 * The whole UI of one player: the HUD (vitals and item hotbar at the bottom centre, the spell bar
 * bottom right, the minimap top right), the inventory dialog, and the carried stack.
 */
class MERIDIANREMASTERED_API SMRHUDRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRHUDRoot) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;

	/** Rebuild everything (style reload). */
	void Rebuild();
	void SetInventoryOpen(bool bOpen);
	void SetInventoryTab(int32 Tab);

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SMRInventoryScreen> Inventory;
	TSharedPtr<SWidget> SpellBar;
	TSharedPtr<SWidget> HotbarArea;
	float SpellBarOpacity = 1.f;

	TSharedRef<SWidget> MakeHotbarArea();
	TSharedRef<SWidget> MakeSpellBar();
	bool IsSpellBarHovered() const;
	EVisibility GetHUDVisibility() const;
};
