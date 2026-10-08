#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UMRUISubsystem;

/**
 * What the HUD draws over the world for the server's objects (docs/adr/0012 M2):
 * - names, as the original (clientd3d d3drender.c): objects with OF_DISPLAY_NAME within 15 squares,
 *   in their name colour (black for the black drawing effect), signs at any distance, the target
 *   always; not through walls;
 * - the target (UMRNetWorldSubsystem::GetTargetId) in brackets, red when it can be attacked;
 * - what the crosshair is on, in fainter corners, and the crosshair itself while the mouse looks.
 */
class MERIDIANREMASTERED_API SMRWorldOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRWorldOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
};
