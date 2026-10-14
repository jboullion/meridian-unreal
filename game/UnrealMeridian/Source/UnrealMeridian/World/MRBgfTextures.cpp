#include "World/MRBgfTextures.h"

#include "Engine/Texture2D.h"
#include "World/MRBgf.h"

UTexture2D* UMRBgfTextures::Get(const TSharedPtr<const FMRBgf>& Bgf, int32 Bitmap)
{
	if (!Bgf.IsValid())
	{
		return nullptr;
	}
	FEntry& E = Entries.FindOrAdd(Bgf.Get());
	E.Bgf = Bgf;
	if (const TObjectPtr<UTexture2D>* Found = E.Bitmaps.Find(Bitmap))
	{
		return *Found;
	}
	UTexture2D* Tex = Bgf->MakeTexture(Bitmap, false, true);
	E.Bitmaps.Add(Bitmap, Tex);
	if (Tex)
	{
		Kept.Add(Tex);
	}
	return Tex;
}
