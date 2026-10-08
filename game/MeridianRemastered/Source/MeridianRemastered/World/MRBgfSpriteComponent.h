#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteData.h"
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
class MERIDIANREMASTERED_API UMRBgfSpriteComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMRBgfSpriteComponent(const FObjectInitializer& ObjectInitializer);

	void SetBgf(TSharedPtr<const FMRBgf> InBgf);
	/** The server's animation records: standing, and while moving (its motion record; Type 0: the same). */
	void SetAnimation(const FMRNetAnimation& Standing, const FMRNetAnimation& Moving);
	/** Moving: show the motion record's animation. */
	void SetMoving(bool bInMoving);
	/** The bitmap shown (tests), or -1. */
	int32 GetShownBitmap() const { return Shown; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Restart();
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
};
