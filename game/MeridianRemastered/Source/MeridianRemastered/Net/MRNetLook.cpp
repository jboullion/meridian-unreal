#include "Net/MRNetLook.h"

#include "Character/MRSpriteData.h"
#include "MeridianRemastered.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"

namespace
{
	// blakston.khd HS_*
	constexpr uint8 HsHead = 1, HsEyes = 11, HsMouth = 12, HsHair = 13, HsNose = 14, HsLegs = 41, HsRightArm = 21, HsLeftArm = 31;

	const FMRNetOverlay* Find(const FMRNetObject& O, uint8 Hotspot)
	{
		return O.OverlayParts.FindByPredicate([Hotspot](const FMRNetOverlay& V) { return V.Hotspot == Hotspot; });
	}

	/** A bitmap the game can draw: converted (an atlas), or "blank" (bald). None otherwise (the look's own). */
	FName UsableBgf(const FString& Bgf)
	{
		if (Bgf.IsEmpty())
		{
			return NAME_None;
		}
		if (Bgf == TEXT("blank") || FMRSpriteLibrary::Get().Atlases.Contains(Bgf))
		{
			return FName(*Bgf);
		}
		static TSet<FString> Logged;
		if (!Logged.Contains(Bgf))
		{
			Logged.Add(Bgf);
			UE_LOG(LogMeridian, Log, TEXT("MRNet: player part %s has no sprite yet; the default is drawn"), *Bgf);
		}
		return NAME_None;
	}

	FName Usable(const FMRNetOverlay* V)
	{
		return V ? UsableBgf(V->Bgf) : NAME_None;
	}

	/** The group an overlay rests on (Kod's, 1-based): a fixed one, or where a one-shot ends. */
	int32 RestGroup(const FMRNetAnimation& A)
	{
		const int32 G = A.Type == MRMsg::ANIMATE_NONE ? A.Group : A.Type == MRMsg::ANIMATE_ONCE ? A.GroupFinal : A.GroupLow;
		return FMath::Max(1, G);
	}
}

int32 MRNetLook::FirstItemOverlay(const FMRNetObject& Object, const FMRNetOverlay** OutHair)
{
	// player.kod SendOverlays: arms, legs, head, mouth, eyes, nose, the hair unless a helmet took
	// it off (poHair_remove), then each item's overlays (SendOverlayInformation)
	const TArray<FMRNetOverlay>& V = Object.OverlayParts;
	const int32 Nose = V.IndexOfByPredicate([](const FMRNetOverlay& O) { return O.Hotspot == HsNose; });
	if (OutHair)
	{
		*OutHair = nullptr;
	}
	if (Nose == INDEX_NONE)
	{
		if (OutHair)
		{
			*OutHair = Find(Object, HsHair);
		}
		return V.Num();  // not a player's list as we know it: no items
	}
	int32 i = Nose + 1;
	if (V.IsValidIndex(i) && V[i].Hotspot == HsHair)
	{
		if (OutHair)
		{
			*OutHair = &V[i];
		}
		++i;
	}
	return i;
}

bool MRNetLook::IsFemale(const FMRNetObject& Object)
{
	const FMRNetOverlay* Head = Find(Object, HsHead);
	if (Head && !Head->Bgf.IsEmpty())
	{
		return Head->Bgf == TEXT("phkx");
	}
	// a torso: bt + a letter, a, c, e... male, b, d, f... female (player.kod, the armour's icons)
	const FString Torso = FPaths::GetBaseFilename(Object.Icon).ToLower();
	return Torso.Len() == 3 && Torso.StartsWith(TEXT("bt")) && (Torso[2] - TEXT('a')) % 2 == 1;
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
	const FMRNetOverlay* Hair = nullptr;
	const int32 Items = FirstItemOverlay(Object, &Hair);
	// no hair overlay at all: a hat or helmet took it off (poHair_remove); bald is "blank" itself
	Out.HairBgf = Hair ? Usable(Hair) : FName(TEXT("blank"));

	// what's worn (docs/adr/0012 M2b): the torso is the object's own bitmap (a shirt's, armour's:
	// SetPlayerIcon), the arms and legs its overlays (SetPlayerArms, SetPlayerLegs), each in the
	// server's translation (piBody_translations; 0 when none is sent)
	Out.BodyBgf = UsableBgf(FPaths::GetBaseFilename(Object.Icon).ToLower());
	const FMRNetOverlay* LeftArm = Find(Object, HsLeftArm);
	const FMRNetOverlay* RightArm = Find(Object, HsRightArm);
	const FMRNetOverlay* Legs = Find(Object, HsLegs);
	Out.LeftArmBgf = Usable(LeftArm);
	Out.RightArmBgf = Usable(RightArm);
	Out.LegsBgf = Usable(Legs);
	Out.BodyXlat = FMath::Max(0, Object.Xlat);
	Out.ArmsXlat = RightArm ? FMath::Max(0, RightArm->Xlat) : (LeftArm ? FMath::Max(0, LeftArm->Xlat) : -1);
	Out.LegsXlat = Legs ? FMath::Max(0, Legs->Xlat) : -1;
	// and what items add: weapons, shields, bows, helmets
	for (int32 i = Items; i < Object.OverlayParts.Num(); ++i)
	{
		const FMRNetOverlay& V = Object.OverlayParts[i];
		const FName Bgf = UsableBgf(V.Bgf);
		if (!Bgf.IsNone() && Bgf != TEXT("blank"))
		{
			FMRSpriteOverlay O;
			O.Bgf = Bgf;
			O.Hotspot = V.Hotspot;
			O.Xlat = FMath::Max(0, V.Xlat);
			O.Group = RestGroup(V.Animation);
			Out.Overlays.Add(O);
		}
	}

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
	if (Legs && Legs->Xlat >= 0)
	{
		Out.Pants = FMRSpriteColours::ClothesOf(Legs->Xlat);
	}
	return true;
}
