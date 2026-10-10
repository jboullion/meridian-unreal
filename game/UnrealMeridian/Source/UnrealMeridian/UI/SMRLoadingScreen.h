#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UMRUISubsystem;
class UMRWarmup;

/**
 * The loading screen over everything while the world warms up (UMRWarmup): the stone window with
 * what is being waited for ("Compiling shaders (120 left)") and a bar. It takes the mouse, so nothing
 * behind it is clicked; UMRUISubsystem removes it when the warm-up is Ready.
 */
class UNREALMERIDIAN_API SMRLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRLoadingScreen) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI, UMRWarmup* InWarmup);

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override { return FReply::Handled(); }

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	TWeakObjectPtr<UMRWarmup> Warmup;
};
