#include "Net/MRNetLook.h"

#include "Character/MRSpriteData.h"
#include "MeridianRemastered.h"
#include "Net/MRNetSubsystem.h"

namespace
{
	// blakston.khd HS_*
	constexpr uint8 HsHead = 1, HsEyes = 11, HsMouth = 12, HsHair = 13, HsNose = 14, HsLegs = 41;

	const FMRNetOverlay* Find(const FMRNetObject& O, uint8 Hotspot)
	{
		return O.OverlayParts.FindByPredicate([Hotspot](const FMRNetOverlay& V) { return V.Hotspot == Hotspot; });
	}

	/** A part the game can draw: converted (an atlas), or "blank" (bald). None otherwise (the look's own). */
	FName Usable(const FMRNetOverlay* V)
	{
		if (!V || V->Bgf.IsEmpty())
		{
			return NAME_None;
		}
		if (V->Bgf == TEXT("blank") || FMRSpriteLibrary::Get().Atlases.Contains(V->Bgf))
		{
			return FName(*V->Bgf);
		}
		static TSet<FString> Logged;
		if (!Logged.Contains(V->Bgf))
		{
			Logged.Add(V->Bgf);
			UE_LOG(LogMeridian, Log, TEXT("MRNet: player part %s has no sprite yet; the default is drawn"), *V->Bgf);
		}
		return NAME_None;
	}
}

bool MRNetLook::IsFemale(const FMRNetObject& Object)
{
	const FMRNetOverlay* Head = Find(Object, HsHead);
	if (Head && !Head->Bgf.IsEmpty())
	{
		return Head->Bgf == TEXT("phkx");
	}
	return FPaths::GetBaseFilename(Object.Icon).ToLower().StartsWith(TEXT("btb"));
}

bool MRNetLook::AppearanceFromObject(const FMRNetObject& Object, FMRSpriteAppearance& Out)
{
	if (!Object.IsPlayer())
	{
		return false;
	}
	Out = FMRSpriteAppearance();
	Out.Look = IsFemale(Object) ? FName(TEXT("player_female")) : FName(TEXT("player_male"));
	if (!FMRSpriteLibrary::Get().Looks.Contains(Out.Look))
	{
		Out.Look = IsFemale(Object) ? FName(TEXT("test_female")) : FName(TEXT("test_male"));  // an older sprite build
	}
	Out.HeadBgf = Usable(Find(Object, HsHead));
	Out.EyesBgf = Usable(Find(Object, HsEyes));
	Out.MouthBgf = Usable(Find(Object, HsMouth));
	Out.NoseBgf = Usable(Find(Object, HsNose));
	const FMRNetOverlay* Hair = Find(Object, HsHair);
	// no hair overlay at all: a hat or helmet took it off (poHair_remove); bald is "blank" itself
	Out.HairBgf = Hair ? Usable(Hair) : FName(TEXT("blank"));

	// colours: the skin from the face (PT_BLUE_TO_SKIN1-4), the hair's from the creator's list
	if (const FMRNetOverlay* Head = Find(Object, HsHead); Head && Head->Xlat >= 1 && Head->Xlat <= FMRSpriteColours::NumSkins)
	{
		Out.Skin = Head->Xlat - 1;
	}
	if (Hair && Hair->Xlat >= 0)
	{
		for (int32 i = 0; i < FMRSpriteColours::NumHair; ++i)
		{
			if (FMRSpriteColours::HairXlat(i) == Hair->Xlat)
			{
				Out.Hair = i;
				break;
			}
		}
	}
	// clothes: two-colour translations (the shirt on the torso, the pants on the legs)
	if (Object.Xlat >= 0)
	{
		const int32 Shirt = FMRSpriteColours::ClothesOf(Object.Xlat);
		Out.Shirt = Shirt;
	}
	if (const FMRNetOverlay* Legs = Find(Object, HsLegs); Legs && Legs->Xlat >= 0)
	{
		Out.Pants = FMRSpriteColours::ClothesOf(Legs->Xlat);
	}
	return true;
}
