#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteData.h"
#include "Net/MRNetWorld.h"
#include "ProceduralMeshComponent.h"
#include "MRBgfSpriteComponent.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;
struct FMRBgf;
struct FMRNetAnimation;

/**
 * An object drawn straight from its original bitmap (docs/adr/0012-client-parity-and-world-coverage.md):
 * the fallback for a creature or item with no sprite of ours, so every object in every room shows,
 * in the original's look. An upright quad turned to the camera, showing the bitmap of the object's
 * current group seen from the camera's side (the original's view slots), its feet on the ground.
 * Textures come from the BGF through the palette; the material is M_RuntimeRoom's.
 */
UCLASS(ClassGroup = Meridian)
class UNREALMERIDIAN_API UMRBgfSpriteComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMRBgfSpriteComponent(const FObjectInitializer& ObjectInitializer);

	void SetBgf(TSharedPtr<const FMRBgf> InBgf);
	/** The server's animation records: standing, and while moving (its motion record; Type 0: the same). */
	void SetAnimation(const FMRNetAnimation& Standing, const FMRNetAnimation& Moving);
	/** Moving: show the motion record's animation. */
	void SetMoving(bool bInMoving);
	/** The original's drawing effect (DRAWFX_*): black (Brightness 0), translucent (Opacity). */
	void SetDrawEffect(uint8 Effect);
	/** The bitmap shown (tests), or -1. */
	int32 GetShownBitmap() const { return Shown; }
	/**
	 * Draw nothing of the object's own bitmap (a prop of the world build stands for it: the faction
	 * flagpole), but keep its size and hotspots: its overlays still hang where the original hung them.
	 */
	void SetBaseHidden(bool bHidden);

	/** One of the server's overlays on the object (a claimed flagpole's flag), drawn at a hotspot. */
	struct FOverlay
	{
		TSharedPtr<const FMRBgf> Bgf;
		int32 Hotspot = 0;
		FMRNetAnimation Animation;
	};
	/**
	 * The overlays drawn on the bitmap, as the original client hangs them (d3drender.c): the
	 * overlay's top left corner at the base bitmap's hotspot, moved by the overlay's own offsets in
	 * the base's pixels, sized by its own shrink; a positive hotspot in front, a negative one behind.
	 */
	void SetOverlays(const TArray<FOverlay>& InOverlays);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Restart();
	/** Put each overlay at its hotspot on the bitmap shown. */
	void PlaceOverlays();
	UTexture2D* TextureOf(int32 Bitmap);
	void Show(int32 Bitmap);

	TSharedPtr<const FMRBgf> Bgf;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FMRSpriteTrackDef StandingDef;
	FMRSpriteTrackDef MovingDef;
	bool bHasMovingDef = false;
	bool bMoving = false;
	FMRSpriteTrack Track;
	int32 Shown = INDEX_NONE;
	bool bBaseHidden = false;
	/** Drawn as another bitmap's overlay: its top left corner at its origin, turned with its parent. */
	bool bIsOverlay = false;
	/** The shown bitmap's scale and feet (Show), for the overlays' hotspots. */
	double CmPerPx = 1.0;
	double FeetX = 0.0;
	double FeetY = 0.0;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMRBgfSpriteComponent>> OverlayComps;
	TArray<int32> OverlayHotspots;
};
