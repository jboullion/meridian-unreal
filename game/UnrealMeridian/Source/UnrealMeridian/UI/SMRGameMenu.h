#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UMRUISubsystem;

/**
 * The Escape menu (docs/adr/0009-user-interface.md): Resume, Options, Log Off (back to the character
 * list, staying connected) and Quit, in the login screen's stone panel over the dimmed world.
 * Esc or F10 opens it (AMRPlayerController) and Esc closes it. Options waits for M9 (docs/parity.md);
 * Log Off shows only while playing on a server.
 */
class UNREALMERIDIAN_API SMRGameMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRGameMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

	/** It has just been shown: take the keyboard. */
	void OnOpened();

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};
