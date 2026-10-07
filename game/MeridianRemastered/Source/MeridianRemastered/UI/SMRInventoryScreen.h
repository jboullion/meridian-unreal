#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SScrollBox;
class SUniformGridPanel;
class SVerticalBox;
class UMRUISubsystem;

/** The inventory dialog's pages, in tab order (the original's stat buttons: statbtn_left_*). */
enum class EMRInventoryTab : uint8 { Inventory, Spells, Skills, Stats, Quests, Count };

/** The player in the dialog: the avatar render target, turned by dragging (the original's 8 angles). */
class MERIDIANREMASTERED_API SMRAvatar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRAvatar) {}
		SLATE_ARGUMENT(FVector2D, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FVector2D Size;
	mutable FSlateBrush Brush;
	float DragAccum = 0.f;
};

/** A tab button: the original's two-state stat button art. */
class MERIDIANREMASTERED_API SMRTab : public SLeafWidget
{
public:
	DECLARE_DELEGATE(FOnClicked);
	SLATE_BEGIN_ARGS(SMRTab) {}
		SLATE_ARGUMENT(FName, Art)  // tab_<art>_up / _down
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_EVENT(FOnClicked, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FName Art;
	TAttribute<bool> bActive;
	FOnClicked OnClicked;
};

/**
 * The inventory dialog (docs/adr/0009-user-interface.md): one stone window with tabs.
 * - Inventory: the avatar between its equipment slots, weight and bulk, the scrollable bag, the hotbar row.
 * - Spells: known spells by school (click or drag one onto the spell bar; shift-click adds it).
 * - Skills: skills with their percentages.  - Stats: the six stats and the pools.  - Quests: none yet.
 * Clicking outside the window drops what the mouse carries; Esc, E or I closes it; a number key
 * over a slot swaps it with that hotbar slot.
 */
class MERIDIANREMASTERED_API SMRInventoryScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRInventoryScreen) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	void OnOpened();
	void SetTab(EMRInventoryTab Tab);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	EMRInventoryTab Tab = EMRInventoryTab::Inventory;
	TSharedPtr<SWidget> Pages;
	TSharedPtr<SWidget> Window;
	TSharedPtr<SUniformGridPanel> BagGrid;
	TSharedPtr<SVerticalBox> SpellList;
	TSharedPtr<SVerticalBox> SkillList;
	int32 BagSlotsShown = 0;
	int32 SpellsShown = -1;
	int32 SkillsShown = -1;

	TSharedRef<SWidget> MakeInventoryPage();
	TSharedRef<SWidget> MakeSpellsPage();
	TSharedRef<SWidget> MakeSkillsPage();
	TSharedRef<SWidget> MakeStatsPage();
	TSharedRef<SWidget> MakeQuestsPage();
	void RebuildBag();
	void RebuildSpells();
	void RebuildSkills();
	FText TabTitle() const;
};
