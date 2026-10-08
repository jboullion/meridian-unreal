#pragma once

#include "CoreMinimal.h"
#include "MRSpriteAppearance.generated.h"

/**
 * How a sprite player looks (docs/sprites.md), replicated on AMRCharacter: a look from
 * data/sprites/player_parts.json, the original character creator's face parts (bgf names; None =
 * the look's own, "blank" = bald) and colours (FMRSpriteColours: skin 0..3, hair 0..13, shirt and
 * pants 0..10; -1 = the look's own) and a height (percent). A handful of bytes, so every client
 * draws every player the same way.
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

	/** The face part overrides by part name (head, hair, eyes, nose, mouth), the set ones only. */
	TMap<FName, FName> PartBgfs() const
	{
		TMap<FName, FName> M;
		const TPair<const TCHAR*, FName> All[] = {{TEXT("head"), HeadBgf}, {TEXT("hair"), HairBgf}, {TEXT("eyes"), EyesBgf},
			{TEXT("nose"), NoseBgf}, {TEXT("mouth"), MouthBgf}};
		for (const TPair<const TCHAR*, FName>& P : All)
		{
			if (!P.Value.IsNone())
			{
				M.Add(FName(P.Key), P.Value);
			}
		}
		return M;
	}

	bool operator==(const FMRSpriteAppearance& O) const
	{
		return Look == O.Look && Skin == O.Skin && Hair == O.Hair && Shirt == O.Shirt && Pants == O.Pants && HeightPct == O.HeightPct
			&& HeadBgf == O.HeadBgf && HairBgf == O.HairBgf && EyesBgf == O.EyesBgf && NoseBgf == O.NoseBgf && MouthBgf == O.MouthBgf;
	}
	bool operator!=(const FMRSpriteAppearance& O) const { return !(*this == O); }
	FString ToString() const
	{
		FString Parts;
		for (const TPair<FName, FName>& P : PartBgfs())
		{
			Parts += FString::Printf(TEXT(" %s=%s"), *P.Key.ToString(), *P.Value.ToString());
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
