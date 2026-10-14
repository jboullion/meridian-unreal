#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRBgfTextures.generated.h"

struct FMRBgf;
class UTexture2D;

/**
 * The bitmaps' textures, one per bgf bitmap for the whole world (UMRBgfSpriteComponent). Each sprite made its
 * own: a room of twenty skulls made twenty textures of each bitmap, on the frame they appeared.
 */
UCLASS()
class UNREALMERIDIAN_API UMRBgfTextures : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The clamped, nearest-filtered texture of one bitmap of a bgf (made the first time). */
	UTexture2D* Get(const TSharedPtr<const FMRBgf>& Bgf, int32 Bitmap);

private:
	struct FEntry
	{
		TSharedPtr<const FMRBgf> Bgf;  // kept, so its address can't be reused by another while it's a key
		TMap<int32, TObjectPtr<UTexture2D>> Bitmaps;
	};
	TMap<const FMRBgf*, FEntry> Entries;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Kept;
};
