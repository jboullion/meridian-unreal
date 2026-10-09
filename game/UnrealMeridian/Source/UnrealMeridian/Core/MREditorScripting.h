#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MREditorScripting.generated.h"

class UTexture;

/**
 * Small helpers for the editor scripts in tools/ue/ (Python: unreal.MREditorScripting), for what
 * Python can't reach on its own.
 */
UCLASS()
class UNREALMERIDIAN_API UMREditorScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Store a texture 16-bit, uncompressed: TC_LQ (BGR565, or BGR555A1 with alpha; DXT1 / DXT5 on
	 * Mac). The value is hidden from the editor's list, so Python's enum doesn't have it.
	 * tools/ue/import_sprites.py stores the sprite ramp atlases this way. Editor only; false elsewhere.
	 */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Editor Scripting")
	static bool SetLowQualityCompression(UTexture* Texture);
};
