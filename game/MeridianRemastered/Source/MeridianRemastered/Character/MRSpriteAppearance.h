#pragma once

#include "CoreMinimal.h"
#include "MRSpriteAppearance.generated.h"

/**
 * How a sprite player looks (docs/sprites.md), replicated on AMRCharacter: a look from
 * data/sprites/player_parts.json, the original character creator's colours (FMRSpriteColours:
 * skin 0..3, hair 0..13, shirt and pants 0..10; -1 = the look's own) and a height (percent).
 * A handful of bytes, so every client draws every player the same way.
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

	bool operator==(const FMRSpriteAppearance& O) const
	{
		return Look == O.Look && Skin == O.Skin && Hair == O.Hair && Shirt == O.Shirt && Pants == O.Pants && HeightPct == O.HeightPct;
	}
	bool operator!=(const FMRSpriteAppearance& O) const { return !(*this == O); }
	FString ToString() const
	{
		return FString::Printf(TEXT("look=%s skin=%d hair=%d shirt=%d pants=%d height=%d%%"),
			*Look.ToString(), Skin, Hair, Shirt, Pants, HeightPct);
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
