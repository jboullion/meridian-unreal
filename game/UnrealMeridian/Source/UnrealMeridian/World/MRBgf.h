#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * An original bitmap file (.bgf, version 10), read as docs/research/bgf-format.md describes it:
 * palette-indexed bitmaps (254 is transparent) with offsets, hotspots and groups.
 */
struct UNREALMERIDIAN_API FMRBgf
{
	struct FHotspot
	{
		int8 Num = 0;
		int32 X = 0;
		int32 Y = 0;
	};
	struct FBitmap
	{
		int32 Width = 0;
		int32 Height = 0;
		int32 XOffset = 0;
		int32 YOffset = 0;
		TArray<FHotspot> Hotspots;
		/** Width * Height palette indices, row by row. */
		TArray<uint8> Pixels;
	};

	static constexpr uint8 Transparent = 254;

	FString Name;
	int32 Shrink = 1;
	TArray<FBitmap> Bitmaps;
	TArray<TArray<int32>> Groups;

	bool Load(const TArray<uint8>& Bytes, FString& OutError);

	/**
	 * A wall or floor texture's size as drawn (grd*.bgf are stored transposed, so width and height
	 * swap), in texels.
	 */
	FIntPoint TextureSize(int32 Bitmap = 0) const;
	/** How many ROO units one repeat of the texture covers: one texel is 1/shrink fine units. */
	FVector2D TextureRepeatRoo(int32 Bitmap = 0) const;
	bool HasTransparency(int32 Bitmap = 0) const;

	/**
	 * The game's palette (data/runtime/palette.bin, tools/bgf2png/bgf2png.py --palette), loaded once.
	 * Empty if the file is missing.
	 */
	static const TArray<FColor>& Palette();

	/**
	 * A transient texture of one bitmap through the palette (index 254 transparent), nearest
	 * filtered and wrapping. bTransposed: as a wall or floor texture (stored transposed).
	 */
	UTexture2D* MakeTexture(int32 Bitmap, bool bTransposed) const;
};
