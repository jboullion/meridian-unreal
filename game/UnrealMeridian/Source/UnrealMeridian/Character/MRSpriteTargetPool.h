#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRSpriteTargetPool.generated.h"

class UTextureRenderTarget2D;

/** A sprite's three render targets (UMRSpriteBodyComponent): the colours, the codes and the ramps, all one size. */
USTRUCT()
struct FMRSpriteTargets
{
	GENERATED_BODY()

	FMRSpriteTargets() = default;
	FMRSpriteTargets(UTextureRenderTarget2D* InColour, UTextureRenderTarget2D* InCode, UTextureRenderTarget2D* InRamp)
		: Colour(InColour), Code(InCode), Ramp(InRamp) {}

	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Colour;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Code;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Ramp;

	bool IsValid() const { return Colour && Code && Ramp; }
	FIntPoint Size() const;
	int64 Bytes() const;
};

/**
 * The sprites' render targets, kept for reuse (docs/performance.md "Packaged game"). Each sprite drew into three
 * targets of its own, made again whenever its look changed or the camera came close enough for a denser one:
 * walking up to someone made new targets mid-play, and the old ones were garbage for the next collection.
 * Now a sprite borrows a set of its size and gives it back; at most mr.Sprite.PoolMB of free sets are kept, and
 * at most two sprites a frame change size for the camera's distance (TryResize).
 */
UCLASS()
class UNREALMERIDIAN_API UMRSpriteTargetPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A set of this size: a free one, or new. Its contents are old: the sprite clears it as it draws. */
	FMRSpriteTargets Acquire(FIntPoint Size);
	/** Give a set back (a sprite resized, changed look or went). */
	void Release(const FMRSpriteTargets& Targets);
	/** May one more sprite resize its targets this frame? */
	bool TryResize();

	/** For a sprite without a world's pool (none should be): a set of its own. */
	static FMRSpriteTargets Make(UObject* Outer, FIntPoint Size);

private:
	UPROPERTY(Transient) TArray<FMRSpriteTargets> Free;
	int64 FreeBytes = 0;
	uint64 ResizeFrame = 0;
	int32 ResizesThisFrame = 0;
};
