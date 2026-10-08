#include "Net/MRCharInfo.h"

#include "Dom/JsonObject.h"
#include "Net/MRResources.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString BgfName(const FString& Resource)
	{
		// "phax.bgf" -> "phax"
		FString Name = FPaths::GetBaseFilename(Resource).ToLower();
		return Name;
	}

	FMRCharPart ReadPart(FMRReader& R, const FMRResourceTable& Resources)
	{
		FMRCharPart P;
		P.Rsc = R.U32();
		P.Bgf = BgfName(Resources.Get(P.Rsc));
		return P;
	}

	// a u32 count then that many resource ids (system.kod AddIconsToPacket)
	bool ReadParts(FMRReader& R, const FMRResourceTable& Resources, TArray<FMRCharPart>& Out)
	{
		const uint32 N = R.U32();
		if (N > 64)
		{
			return false;
		}
		for (uint32 i = 0; i < N && R.IsOk(); ++i)
		{
			Out.Add(ReadPart(R, Resources));
		}
		return R.IsOk();
	}

	// hair, head, eyes, noses, mouths (system.kod AddFaceIconsToPacket)
	bool ReadFaces(FMRReader& R, const FMRResourceTable& Resources, FMRCharFaces& Out)
	{
		if (!ReadParts(R, Resources, Out.Hair))
		{
			return false;
		}
		Out.Head = ReadPart(R, Resources);
		return ReadParts(R, Resources, Out.Eyes) && ReadParts(R, Resources, Out.Noses) && ReadParts(R, Resources, Out.Mouths);
	}

	// u32 count, then per entry: number, name, description, cost (u32), school (u8)
	bool ReadAbilities(FMRReader& R, const FMRResourceTable& Resources, TArray<FMRCharAbility>& Out)
	{
		const uint32 N = R.U32();
		if (N > 1024)
		{
			return false;
		}
		for (uint32 i = 0; i < N && R.IsOk(); ++i)
		{
			FMRCharAbility A;
			A.Num = R.U32();
			A.Name = Resources.Get(R.U32());
			A.Desc = MRServerText::StripStyle(Resources.Get(R.U32()));
			A.Cost = static_cast<int32>(R.U32());
			A.School = R.U8();
			Out.Add(MoveTemp(A));
		}
		return R.IsOk();
	}

	bool ReadXlats(FMRReader& R, TArray<uint8>& Out)
	{
		const int32 N = R.U8();
		for (int32 i = 0; i < N && R.IsOk(); ++i)
		{
			Out.Add(R.U8());
		}
		return R.IsOk();
	}

	const FMRCharAbility* FindAbility(const TArray<FMRCharAbility>& List, uint32 Num)
	{
		return List.FindByPredicate([Num](const FMRCharAbility& A) { return A.Num == Num; });
	}
}

bool MRCharInfo::Parse(FMRReader& R, const FMRResourceTable& Resources, FMRCharInfo& Out)
{
	// char.c HandleCharInfo: hair colours, skins, the male then the female face options, spells,
	// skills; the client refuses the message if anything is left over
	Out = FMRCharInfo();
	return ReadXlats(R, Out.HairXlats) && ReadXlats(R, Out.SkinXlats)
		&& ReadFaces(R, Resources, Out.Male) && ReadFaces(R, Resources, Out.Female)
		&& ReadAbilities(R, Resources, Out.Spells) && ReadAbilities(R, Resources, Out.Skills)
		&& R.AtEnd();
}

FMRWriter MRCharInfo::Write(const FMRNewCharacter& C, const FMRCharInfo& Info)
{
	const FMRCharFaces& F = Info.Faces(C.bFemale);
	auto Rsc = [](const TArray<FMRCharPart>& List, int32 Index) { return List.IsValidIndex(Index) ? List[Index].Rsc : 0u; };
	FMRWriter W(MRMsg::BP_SYSTEM);
	W.U8(MRMsg::BP_NEW_CHARINFO);
	W.U32(C.SlotId).Str(C.Name.TrimStartAndEnd()).Str(C.Description);
	W.U8(C.bFemale ? 2 : 1);  // GENDER_MALE 1, GENDER_FEMALE 2
	// exactly five parts, or the server forces a male default face (player.kod PlayerNewCharInfo)
	W.U16(5);
	W.U32(F.Head.Rsc).U32(Rsc(F.Hair, C.Hair)).U32(Rsc(F.Eyes, C.Eyes)).U32(Rsc(F.Noses, C.Nose)).U32(Rsc(F.Mouths, C.Mouth));
	// the translations themselves, not their indexes (charmake.c)
	W.U8(Info.HairXlats.IsValidIndex(C.HairColour) ? Info.HairXlats[C.HairColour] : 0);
	W.U8(Info.SkinXlats.IsValidIndex(C.Skin) ? Info.SkinXlats[C.Skin] : 1);
	W.U16(NumStats);
	for (int32 i = 0; i < NumStats; ++i)
	{
		W.I32(C.Stats[i]);
	}
	W.U16(static_cast<uint16>(C.Spells.Num()));
	for (uint32 S : C.Spells)
	{
		W.U32(S);
	}
	W.U16(static_cast<uint16>(C.Skills.Num()));
	for (uint32 S : C.Skills)
	{
		W.U32(S);
	}
	return W;
}

bool MRCharInfo::LoadMock(const FString& Json, FMRCharInfo& Out)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return false;
	}
	Out = FMRCharInfo();
	auto Bytes = [&Root](const TCHAR* Field, TArray<uint8>& Into)
	{
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (Root->TryGetArrayField(Field, A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				Into.Add(static_cast<uint8>(V->AsNumber()));
			}
		}
	};
	Bytes(TEXT("hair_xlats"), Out.HairXlats);
	Bytes(TEXT("skin_xlats"), Out.SkinXlats);
	const TSharedPtr<FJsonObject>* Faces;
	if (Root->TryGetObjectField(TEXT("faces"), Faces))
	{
		auto Gender = [](const TSharedPtr<FJsonObject>& G, FMRCharFaces& Into)
		{
			auto List = [&G](const TCHAR* Field, TArray<FMRCharPart>& L)
			{
				const TArray<TSharedPtr<FJsonValue>>* A;
				if (G.IsValid() && G->TryGetArrayField(Field, A))
				{
					for (const TSharedPtr<FJsonValue>& V : *A)
					{
						L.Add(FMRCharPart{0, V->AsString()});
					}
				}
			};
			TArray<FMRCharPart> Heads;
			List(TEXT("head"), Heads);
			if (Heads.Num() > 0)
			{
				Into.Head = Heads[0];
			}
			List(TEXT("hair"), Into.Hair);
			List(TEXT("eyes"), Into.Eyes);
			List(TEXT("nose"), Into.Noses);
			List(TEXT("mouth"), Into.Mouths);
		};
		Gender((*Faces)->GetObjectField(TEXT("male")), Out.Male);
		Gender((*Faces)->GetObjectField(TEXT("female")), Out.Female);
	}
	auto Abilities = [&Root](const TCHAR* Field, TArray<FMRCharAbility>& Into)
	{
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (Root->TryGetArrayField(Field, A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMRCharAbility Ab;
				Ab.Num = static_cast<uint32>(O->GetNumberField(TEXT("num")));
				O->TryGetStringField(TEXT("name"), Ab.Name);
				O->TryGetStringField(TEXT("desc"), Ab.Desc);
				Ab.Cost = static_cast<int32>(O->GetNumberField(TEXT("cost")));
				Ab.School = static_cast<uint8>(O->GetNumberField(TEXT("school")));
				Into.Add(MoveTemp(Ab));
			}
		}
	};
	Abilities(TEXT("spells"), Out.Spells);
	Abilities(TEXT("skills"), Out.Skills);
	return Out.IsValid();
}

bool MRCharInfo::IsLegalNameChar(TCHAR C)
{
	if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))
	{
		return true;
	}
	// Latin-1 letters, without the multiplication and division signs
	if (C >= 0xC0 && C <= 0xFF)
	{
		return C != 0xD7 && C != 0xF7;
	}
	static const TCHAR* Symbols = TEXT("_ '!@$^&*()+=:[]{};/?|<>");
	for (const TCHAR* S = Symbols; *S; ++S)
	{
		if (*S == C)
		{
			return true;
		}
	}
	return false;
}

int32 MRCharInfo::StatPointsLeft(const FMRNewCharacter& C)
{
	int32 Sum = 0;
	for (int32 i = 0; i < NumStats; ++i)
	{
		Sum += C.Stats[i];
	}
	return StatTotal - Sum;
}

int32 MRCharInfo::AbilityPointsLeft(const FMRNewCharacter& C, const FMRCharInfo& Info)
{
	int32 Spent = 0;
	for (uint32 S : C.Spells)
	{
		const FMRCharAbility* A = FindAbility(Info.Spells, S);
		Spent += A ? A->Cost : 0;
	}
	for (uint32 S : C.Skills)
	{
		const FMRCharAbility* A = FindAbility(Info.Skills, S);
		Spent += A ? A->Cost : 0;
	}
	return AbilityPoints - Spent;
}

MRCharInfo::EProblem MRCharInfo::Validate(const FMRNewCharacter& C, const FMRCharInfo& Info, FString& OutMessage)
{
	const FString Name = C.Name.TrimStartAndEnd();
	if (Name.Len() < NameMin || Name.Len() > NameMax)
	{
		OutMessage = FString::Printf(TEXT("Your name must be %d to %d letters long."), NameMin, NameMax);
		return EProblem::Name;
	}
	for (TCHAR Ch : Name)
	{
		if (!IsLegalNameChar(Ch))
		{
			OutMessage = FString::Printf(TEXT("Your name can't contain \"%c\"."), Ch);
			return EProblem::Name;
		}
	}
	if (C.Description.Len() > DescriptionMax)
	{
		OutMessage = FString::Printf(TEXT("The description is too long (%d letters at most)."), DescriptionMax);
		return EProblem::Description;
	}
	for (int32 i = 0; i < NumStats; ++i)
	{
		if (C.Stats[i] < StatMin || C.Stats[i] > StatMax)
		{
			OutMessage = FString::Printf(TEXT("Each statistic must be from %d to %d."), StatMin, StatMax);
			return EProblem::Stats;
		}
	}
	if (StatPointsLeft(C) < 0)
	{
		OutMessage = TEXT("You have spent more statistic points than you have.");
		return EProblem::Stats;
	}
	if (AbilityPointsLeft(C, Info) < 0)
	{
		OutMessage = TEXT("You have chosen more spells and skills than your points allow.");
		return EProblem::Abilities;
	}
	bool bShalille = false, bQor = false;
	for (uint32 S : C.Spells)
	{
		if (const FMRCharAbility* A = FindAbility(Info.Spells, S))
		{
			bShalille |= A->School == SchoolShalille;
			bQor |= A->School == SchoolQor;
		}
	}
	if (bShalille && bQor)
	{
		OutMessage = TEXT("Shal'ille and Qor spells can't be chosen together.");
		return EProblem::Abilities;
	}
	OutMessage.Reset();
	return EProblem::None;
}

void MRCharInfo::ClampParts(FMRNewCharacter& C, const FMRCharInfo& Info)
{
	const FMRCharFaces& F = Info.Faces(C.bFemale);
	auto Wrap = [](int32 I, int32 N) { return N > 0 ? ((I % N) + N) % N : 0; };
	C.Hair = Wrap(C.Hair, F.Hair.Num());
	C.Eyes = Wrap(C.Eyes, F.Eyes.Num());
	C.Nose = Wrap(C.Nose, F.Noses.Num());
	C.Mouth = Wrap(C.Mouth, F.Mouths.Num());
	C.HairColour = Wrap(C.HairColour, Info.HairXlats.Num());
	C.Skin = Wrap(C.Skin, Info.SkinXlats.Num());
}

void MRCharInfo::Randomize(FMRNewCharacter& C, const FMRCharInfo& Info, FRandomStream& Rng)
{
	C.bFemale = Rng.RandRange(0, 1) == 1;
	const FMRCharFaces& F = Info.Faces(C.bFemale);
	C.Hair = Rng.RandRange(0, FMath::Max(0, F.Hair.Num() - 1));
	C.Eyes = Rng.RandRange(0, FMath::Max(0, F.Eyes.Num() - 1));
	C.Nose = Rng.RandRange(0, FMath::Max(0, F.Noses.Num() - 1));
	C.Mouth = Rng.RandRange(0, FMath::Max(0, F.Mouths.Num() - 1));
	C.HairColour = Rng.RandRange(0, FMath::Max(0, Info.HairXlats.Num() - 1));
	C.Skin = Rng.RandRange(0, FMath::Max(0, Info.SkinXlats.Num() - 1));
}
