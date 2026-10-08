#pragma once

#include "CoreMinimal.h"
#include "MRSpriteAppearance.generated.h"

/**
 * Something an item puts on a player (docs/adr/0012 M2b; player.kod SendOverlays, an item's
 * SendOverlayInformation): a weapon, shield, bow or helmet, on a hotspot of the body (blakston.khd
 * HS_*), in a palette translation, resting on a group (Kod's, 1-based).
 */
USTRUCT(BlueprintType)
struct FMRSpriteOverlay
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName Bgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") uint8 Hotspot = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Xlat = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Group = 1;

	bool operator==(const FMRSpriteOverlay& O) const { return Bgf == O.Bgf && Hotspot == O.Hotspot && Xlat == O.Xlat && Group == O.Group; }
};

/**
 * How a sprite player looks (docs/sprites.md), replicated on AMRCharacter: a look from
 * data/sprites/player_parts.json, the original character creator's face parts (bgf names; None =
 * the look's own, "blank" = bald) and colours (FMRSpriteColours: skin 0..3, hair 0..13, shirt and
 * pants 0..10; -1 = the look's own) and a height (percent). A handful of bytes, so every client
 * draws every player the same way.
 *
 * Equipment (online, from the server's overlays: MRNetLook): the torso, arms and legs an item
 * swaps in (armour, gauntlets, robes), their palette translations as the server sends them (-1 =
 * from the colours above), and the overlays items add (weapon, shield, helmet).
 */
USTRUCT(BlueprintType)
struct FMRSpriteAppearance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName Look;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Skin = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Hair = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Shirt = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 Pants = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 HeightPct = 100;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName HeadBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName HairBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName EyesBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName NoseBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName MouthBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName BodyBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName LeftArmBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName RightArmBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") FName LegsBgf;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 BodyXlat = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 ArmsXlat = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") int32 LegsXlat = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Sprite") TArray<FMRSpriteOverlay> Overlays;

	/** The part overrides by part name (head, hair, eyes, nose, mouth, body, arms, legs), the set ones only. */
	TMap<FName, FName> PartBgfs() const
	{
		TMap<FName, FName> M;
		const TPair<const TCHAR*, FName> All[] = {{TEXT("head"), HeadBgf}, {TEXT("hair"), HairBgf}, {TEXT("eyes"), EyesBgf},
			{TEXT("nose"), NoseBgf}, {TEXT("mouth"), MouthBgf}, {TEXT("body"), BodyBgf}, {TEXT("left_arm"), LeftArmBgf},
			{TEXT("right_arm"), RightArmBgf}, {TEXT("legs"), LegsBgf}};
		for (const TPair<const TCHAR*, FName>& P : All)
		{
			if (!P.Value.IsNone())
			{
				M.Add(FName(P.Key), P.Value);
			}
		}
		return M;
	}

	/** The palette translations the server gave (part name -> xlat), the set ones only. */
	TMap<FName, int32> PartXlats() const
	{
		TMap<FName, int32> M;
		if (BodyXlat >= 0) { M.Add(TEXT("body"), BodyXlat); }
		if (ArmsXlat >= 0) { M.Add(TEXT("left_arm"), ArmsXlat); M.Add(TEXT("right_arm"), ArmsXlat); }
		if (LegsXlat >= 0) { M.Add(TEXT("legs"), LegsXlat); }
		return M;
	}

	bool operator==(const FMRSpriteAppearance& O) const
	{
		return Look == O.Look && Skin == O.Skin && Hair == O.Hair && Shirt == O.Shirt && Pants == O.Pants && HeightPct == O.HeightPct
			&& HeadBgf == O.HeadBgf && HairBgf == O.HairBgf && EyesBgf == O.EyesBgf && NoseBgf == O.NoseBgf && MouthBgf == O.MouthBgf
			&& BodyBgf == O.BodyBgf && LeftArmBgf == O.LeftArmBgf && RightArmBgf == O.RightArmBgf && LegsBgf == O.LegsBgf
			&& BodyXlat == O.BodyXlat && ArmsXlat == O.ArmsXlat && LegsXlat == O.LegsXlat && Overlays == O.Overlays;
	}
	bool operator!=(const FMRSpriteAppearance& O) const { return !(*this == O); }
	FString ToString() const
	{
		FString Parts;
		for (const TPair<FName, FName>& P : PartBgfs())
		{
			Parts += FString::Printf(TEXT(" %s=%s"), *P.Key.ToString(), *P.Value.ToString());
		}
		for (const FMRSpriteOverlay& O : Overlays)
		{
			Parts += FString::Printf(TEXT(" %s@%d"), *O.Bgf.ToString(), O.Hotspot);
		}
		return FString::Printf(TEXT("look=%s skin=%d hair=%d shirt=%d pants=%d height=%d%%%s"),
			*Look.ToString(), Skin, Hair, Shirt, Pants, HeightPct, *Parts);
	}
};

/**
 * The action a sprite player plays (an attack, an emote, a dance; None = stop), replicated so the
 * others see it. Seq changes with every play, so the same emote twice still replicates; ServerTime
 * lets a late joiner skip one-shots that are long over.
 */
USTRUCT()
struct FMRSpriteActionState
{
	GENERATED_BODY()

	UPROPERTY() FName Action;
	UPROPERTY() uint8 Seq = 0;
	UPROPERTY() float ServerTime = 0.f;
};
