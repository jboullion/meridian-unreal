#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class SMRSelectList;
class SMRTextBox;
class STextBlock;
class SVerticalBox;
class UMRUISubsystem;
struct FMRNetDescription;

/**
 * The Look dialog (docs/adr/0012 M2; the original's DisplayDescription): an object's or a player's
 * description from the server (BP_LOOK, UC_LOOK_PLAYER) with its picture, an inscription (signs,
 * scrolls), a player's extra lines and web page, and one's own description to edit
 * (BP_CHANGE_DESCRIPTION). With several things under the crosshair it first lists them to pick from.
 * Right mouse button opens it (AMRPlayerController::OnLookKey); Esc or Close shuts it.
 */
class MERIDIANREMASTERED_API SMRLookDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRLookDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** Show a description; Picture is drawn in the box at the left (may be empty). */
	void ShowDescription(const FMRNetDescription& D, const FSlateBrush* Picture);
	/** List the objects to pick from (their ids and names); picking one asks for its description. */
	void ShowPicker(const TArray<uint32>& Ids, const TArray<FText>& Names);
	/** The edited description (one's own), for tests. */
	FString GetEditedText() const;
	void SetEditedText(const FString& Text);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	/** Body under the title; Extra: a button left of Close (Save). */
	void Rebuild(TSharedRef<SWidget> Body, const FText& Title, const FLinearColor& TitleColor, TSharedPtr<SWidget> Extra);
	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SMRTextBox> EditBox;
	uint32 EditId = 0;
	TArray<uint32> PickIds;
};
