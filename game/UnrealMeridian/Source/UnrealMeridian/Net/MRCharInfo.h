#pragma once

#include "CoreMinimal.h"
#include "Net/MRProtocol.h"

class FMRResourceTable;

/**
 * Character creation (docs/research/blakserv-protocol.md "Character creation"): what the server
 * offers (BP_CHARINFO, built by Kod's System SendCharInfo) and the new character the client sends
 * back (BP_SYSTEM + BP_NEW_CHARINFO, read by blakserv sprocket.c and Kod's player.kod
 * PlayerNewCharInfo). Pure data: UMRNetSubsystem sends it, the creator dialog edits it.
 */

/** A face part or hair the server offers: its resource id (what is sent back) and bgf name ("phax"; "blank" = bald). */
struct FMRCharPart
{
	uint32 Rsc = 0;
	FString Bgf;
};

/** One gender's options, in the server's order (the first is its default). */
struct FMRCharFaces
{
	TArray<FMRCharPart> Hair;
	FMRCharPart Head;
	TArray<FMRCharPart> Eyes;
	TArray<FMRCharPart> Noses;
	TArray<FMRCharPart> Mouths;
};

/** A spell or skill a new character may start with. */
struct FMRCharAbility
{
	uint32 Num = 0;       // spell / skill number (SID_* / SKID_*): what is sent back
	FString Name;
	FString Desc;
	int32 Cost = 10;      // creation points: 25 for level 2, else 10
	uint8 School = 0;     // SS_* (1 Shal'ille .. 6 Jala) or SKS_* (7 crafting, 10 weaponcraft...)
	/** Level from the cost, as the original client lists them ("Faren 2: ..."). */
	int32 Level() const { return Cost >= 25 ? 2 : 1; }
};

/** BP_CHARINFO: everything the creator offers. */
struct FMRCharInfo
{
	TArray<uint8> HairXlats;    // the 14 hair colours (palette translations)
	TArray<uint8> SkinXlats;    // the 4 skins, light to dark
	FMRCharFaces Male;
	FMRCharFaces Female;
	TArray<FMRCharAbility> Spells;
	TArray<FMRCharAbility> Skills;

	bool IsValid() const { return SkinXlats.Num() > 0 && Male.Eyes.Num() > 0; }
	const FMRCharFaces& Faces(bool bFemale) const { return bFemale ? Female : Male; }
};

/** The new character (BP_NEW_CHARINFO). Indexes are into the FMRCharInfo lists. */
struct FMRNewCharacter
{
	uint32 SlotId = 0;
	FString Name;
	FString Description;
	bool bFemale = false;
	int32 Hair = 0, Eyes = 0, Nose = 0, Mouth = 0;
	int32 HairColour = 0;       // index into HairXlats
	int32 Skin = 0;             // index into SkinXlats
	/** Might, Intellect, Stamina, Agility, Mysticism, Aim (charstat.c order). */
	int32 Stats[6] = {25, 25, 25, 25, 25, 25};
	TArray<uint32> Spells;      // spell numbers
	TArray<uint32> Skills;      // skill numbers
};

namespace MRCharInfo
{
	// the rules (module/char/char.h, charstat.c, player.kod PlayerNewCharInfo, blakston.khd)
	constexpr int32 NumStats = 6;
	constexpr int32 StatMin = 1;
	constexpr int32 StatMax = 50;
	constexpr int32 StatStart = 25;
	constexpr int32 StatTotal = 220;          // 6 x 25 + the 70 points to spend
	constexpr int32 AbilityPoints = 45;       // spells and skills together
	constexpr int32 NameMin = 3;
	constexpr int32 NameMax = 30;
	constexpr int32 DescriptionMax = 999;
	constexpr uint8 SchoolShalille = 1;
	constexpr uint8 SchoolQor = 2;

	/** Read a BP_CHARINFO body (the reader past the type byte). False if it is cut short or too long. */
	UNREALMERIDIAN_API bool Parse(FMRReader& R, const FMRResourceTable& Resources, FMRCharInfo& Out);

	/** The BP_SYSTEM + BP_NEW_CHARINFO message (charmake.c, sprocket.c): face parts head, hair, eyes, nose, mouth. */
	UNREALMERIDIAN_API FMRWriter Write(const FMRNewCharacter& C, const FMRCharInfo& Info);

	/** The offline stand-in (data/charinfo.json, from Kod): resource ids are 0. */
	UNREALMERIDIAN_API bool LoadMock(const FString& Json, FMRCharInfo& Out);

	/** The original's legal name characters (charname.c, Kod ValidateUserName). */
	UNREALMERIDIAN_API bool IsLegalNameChar(TCHAR C);

	enum class EProblem : uint8 { None, Name, Description, Stats, Abilities };
	/**
	 * What the server would refuse or silently replace: the name (3-30 legal characters), the
	 * description, the stats (each 1-50, total at most 220), the spells and skills (at most 45
	 * points; Shal'ille and Qor not together, the original client's rule). OutMessage says why.
	 */
	UNREALMERIDIAN_API EProblem Validate(const FMRNewCharacter& C, const FMRCharInfo& Info, FString& OutMessage);

	UNREALMERIDIAN_API int32 StatPointsLeft(const FMRNewCharacter& C);
	UNREALMERIDIAN_API int32 AbilityPointsLeft(const FMRNewCharacter& C, const FMRCharInfo& Info);

	/** A random face and colours, as the original creator starts (charface.c). */
	UNREALMERIDIAN_API void Randomize(FMRNewCharacter& C, const FMRCharInfo& Info, FRandomStream& Rng);

	/** Clamp the part indexes to the gender's lists (switching gender, charface.c). */
	UNREALMERIDIAN_API void ClampParts(FMRNewCharacter& C, const FMRCharInfo& Info);
}
