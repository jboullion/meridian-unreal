#include "Core/MREditorScripting.h"

#include "Engine/Texture.h"

bool UMREditorScripting::SetLowQualityCompression(UTexture* Texture)
{
#if WITH_EDITOR
	if (!Texture)
	{
		return false;
	}
	Texture->Modify();
	Texture->CompressionSettings = TC_LQ;
	Texture->PostEditChange();
	return Texture->CompressionSettings == TC_LQ;
#else
	return false;
#endif
}
