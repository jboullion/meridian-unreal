#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UMRUISubsystem;
class UMRZoneSubsystem;
class UTexture2D;

/**
 * Minimap data shared by the capture (-MRMapCapture, UMRMapCapture) and the widget
 * (docs/adr/0009-user-interface.md): one top-down picture per geometry zone, a square over the
 * footprint of every zone drawn from that geometry (Raza and the Outskirts share one), north up.
 * data/ui/minimap.json: defaults and per-zone overrides (margin, cut height, skip).
 */
namespace MRMinimap
{
	struct FZoneSettings
	{
		/** Extra border around the footprint, as a fraction of its larger side. */
		float Margin = 0.04f;
		/** Interiors: the camera sits this far above the arrival point's floor, so ceilings are behind it (cm); 0 = from above everything. */
		float CutHeightCm = 0.f;
		/** Capture the albedo instead of the lit picture (interiors: Lumen and the zone's ambient don't light an ortho view). */
		bool bBaseColor = false;
		/** Leave out the placed props, fires and effects (ZoneProp, ZoneFire, ZoneEffect tags): interiors read as a floor plan. */
		bool bHideProps = false;
		/** Draw shadows (off by default: a map reads better without them). */
		bool bShadows = false;
		bool bSkip = false;
		int32 Resolution = 2048;
	};

	MERIDIANREMASTERED_API FZoneSettings Settings(int32 GeometryRid);
	/** The square the picture covers, world XY (UE cm): image left = -X... right = +X, top = -Y (north). */
	MERIDIANREMASTERED_API bool CaptureRect(const UMRZoneSubsystem* Zones, int32 GeometryRid, FBox2D& Out);
	/** /Game/Generated/UI/Minimap/T_Map_<rid> */
	MERIDIANREMASTERED_API FString TexturePath(int32 GeometryRid);
	/** The map style chosen in minimap.json ("photo", "walls", "parchment"): which imported file is used. */
	MERIDIANREMASTERED_API FString Style();
}

/**
 * The minimap (top right): the zone's top-down picture around the player, north up, in the
 * original map's metal rim on parchment, with the player as an arrow turned to where they face.
 * - and = zoom (UMRUISubsystem::OnMapZoom). The zone's name and the Meridian time sit under it.
 */
class MERIDIANREMASTERED_API SMRMinimap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRMinimap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	int32 GeometryRid = -1;
	FBox2D Rect = FBox2D(ForceInit);
	mutable FSlateBrush MapBrush;
	FText ZoneName;
	float Zoom = 1.f;
};
