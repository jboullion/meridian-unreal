#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SMultiLineEditableText;
class SScrollBox;
class SVerticalBox;
class UMRUISubsystem;

/**
 * Input controls for dialogs (the character creator), drawn with the HUD's art
 * (docs/adr/0009-user-interface.md): the original's controls were plain Win32 ones (module/char/char.rc).
 */

/**
 * A draggable bar (the original creator's stat sliders, graphctl.c's slider mode): the stat bar art,
 * an integer value from Min to Max, set by clicking or dragging along it; a marker under the value.
 */
class MERIDIANREMASTERED_API SMRSlider : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnValue, int32);
	SLATE_BEGIN_ARGS(SMRSlider) : _Min(0), _Max(10), _Width(90.f), _Height(9.f), _bShowValue(true), _Ticks(0), _bTrack(false) {}
		SLATE_ATTRIBUTE(int32, Value)
		SLATE_ARGUMENT(int32, Min)
		SLATE_ATTRIBUTE(int32, Max)
		/** In original pixels. */
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(bool, bShowValue)
		/** The fill (default: the original's stat bar green, ui_style.json graph_bar). */
		SLATE_ARGUMENT(TOptional<FLinearColor>, Color)
		/** Steps (ticks under the bar, e.g. the skin slider's four); 0 = none. */
		SLATE_ATTRIBUTE(int32, Ticks)
		/** A slider rather than a bar: a gold line with its ticks and a thumb on the value. */
		SLATE_ARGUMENT(bool, bTrack)
		SLATE_EVENT(FOnValue, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	void SetFromMouse(const FGeometry& Geo, const FPointerEvent& Event);
	TWeakObjectPtr<UMRUISubsystem> UI;
	TAttribute<int32> Value;
	TAttribute<int32> Max;
	TAttribute<int32> Ticks;
	int32 Min = 0;
	int32 GetMax() const { return FMath::Max(Max.Get(), Min + 1); }
	float Width = 90.f, Height = 9.f;
	bool bShowValue = true;
	TOptional<FLinearColor> Color;
	bool bTrack = false;
	FOnValue OnChanged;
	int32 PaintTrack(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FLinearColor& Tint) const;
};

/** A multi-line text box (the character's description): the UI's font inside an inset panel. */
class MERIDIANREMASTERED_API SMRTextBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRTextBox) : _Width(200.f), _Height(60.f), _MaxLength(999) {}
		SLATE_ARGUMENT(FText, InitialText)
		SLATE_ARGUMENT(FText, HintText)
		/** In original pixels. */
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(int32, MaxLength)
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	FString GetText() const;
	void SetText(const FString& InText);
	void Focus();
	/** A click anywhere in the box puts the cursor in the text. */
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override;

private:
	TSharedPtr<SMultiLineEditableText> Edit;
	/** The UI's font and text colour (as SMRTextField's), kept alive for the edit. */
	FTextBlockStyle TextStyle;
	int32 MaxLength = 999;
	FSimpleDelegate OnChanged;
};

/**
 * A list to pick from (the creator's spell and skill lists): rows of text on a dark inset, the
 * selected one highlighted; a click selects, a second click on the selected row activates it.
 */
class MERIDIANREMASTERED_API SMRSelectList : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnRow, int32);
	SLATE_BEGIN_ARGS(SMRSelectList) : _Width(120.f), _Height(100.f), _TextSize(9.f) {}
		/** In original pixels. */
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, TextSize)
		SLATE_EVENT(FOnRow, OnSelected)
		/** The selected row clicked again (or double-clicked). */
		SLATE_EVENT(FOnRow, OnActivated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** Replace the rows (Selected is kept in range; -1 = none). */
	void SetItems(const TArray<FText>& Items, const TArray<bool>& Enabled = TArray<bool>());
	int32 GetSelected() const { return Selected; }
	void SetSelected(int32 Index);

private:
	void Rebuild();
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SVerticalBox> Rows;
	TArray<FText> Items;
	TArray<bool> Enabled;
	int32 Selected = -1;
	float TextSize = 9.f;
	FOnRow OnSelected;
	FOnRow OnActivated;
};
