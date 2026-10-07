#include "UI/MRGameData.h"

#include "Dom/JsonObject.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> LoadArray(const TCHAR* Name)
	{
		FString Text;
		TArray<TSharedPtr<FJsonValue>> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), Name)))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("UI data: %s not found (run tools/kod_extract/extract.py)"), Name);
		}
		return Root;
	}

	int32 Int(const FJsonObject& O, const TCHAR* Key)
	{
		double V = 0.0;
		return O.TryGetNumberField(Key, V) ? static_cast<int32>(V) : 0;
	}

	FString Str(const FJsonObject& O, const TCHAR* Key)
	{
		FString V;
		O.TryGetStringField(Key, V);
		return V;
	}

	/** "mmant.bgf" -> mmant (icon textures are named by the lower-case bgf stem). */
	FName IconName(const FString& Bgf)
	{
		FString S = Bgf.ToLower();
		S.RemoveFromEnd(TEXT(".bgf"));
		return S.IsEmpty() ? NAME_None : FName(*S);
	}

	/** Kod descriptions may hold %s / %i placeholders (filled in by the server): drop them. */
	FText Desc(const FString& In)
	{
		FString S = In.Replace(TEXT("%s"), TEXT("")).Replace(TEXT("%i"), TEXT("")).Replace(TEXT("%q"), TEXT(""));
		S.ReplaceInline(TEXT("  "), TEXT(" "));
		return FText::FromString(S.TrimStartAndEnd());
	}

	FText Capitalised(const FString& In)
	{
		FString S = In;
		if (S.Len() > 0)
		{
			S[0] = FChar::ToUpper(S[0]);
		}
		return FText::FromString(S);
	}
}

void UMRGameDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	for (const TSharedPtr<FJsonValue>& V : LoadArray(TEXT("items.json")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid() || Str(*O, TEXT("vrName")).IsEmpty())
		{
			continue;  // abstract base classes (Armor, Helmet...) have no name
		}
		FMRItemDef D;
		D.Class = FName(*Str(*O, TEXT("class")));
		D.Name = Capitalised(Str(*O, TEXT("vrName")));
		D.Desc = Desc(Str(*O, TEXT("vrDesc")));
		D.Icon = IconName(Str(*O, TEXT("vrIcon")));
		D.Kind = Str(*O, TEXT("kind"));
		D.Weight = Int(*O, TEXT("viWeight"));
		D.Bulk = Int(*O, TEXT("viBulk"));
		D.Value = Int(*O, TEXT("viValue_average"));
		D.UseType = Int(*O, TEXT("viUse_type"));
		D.Defense = Int(*O, TEXT("viDefense_base"));
		const TSharedPtr<FJsonObject>* Vars = nullptr;
		if (O->TryGetObjectField(TEXT("vars"), Vars))
		{
			D.DamageMin = Int(**Vars, TEXT("viDamage_min"));
			D.DamageMax = Int(**Vars, TEXT("viDamage_max"));
		}
		const TArray<TSharedPtr<FJsonValue>>* Parents = nullptr;
		if (O->TryGetArrayField(TEXT("parents"), Parents))
		{
			for (const TSharedPtr<FJsonValue>& P : *Parents)
			{
				D.bStackable |= P->AsString() == TEXT("NumberItem");
			}
		}
		Items.Add(D.Class, MoveTemp(D));
	}

	for (const TSharedPtr<FJsonValue>& V : LoadArray(TEXT("spells.json")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		FMRSpellDef D;
		D.Class = FName(*Str(*O, TEXT("class")));
		D.Name = Capitalised(Str(*O, TEXT("vrName")));
		D.Desc = Desc(Str(*O, TEXT("vrDesc")));
		D.Icon = IconName(Str(*O, TEXT("vrIcon")));
		D.School = Int(*O, TEXT("viSchool"));
		D.Level = Int(*O, TEXT("viSpell_level"));
		D.Mana = Int(*O, TEXT("viMana"));
		const TArray<TSharedPtr<FJsonValue>>* Reagents = nullptr;
		if (O->TryGetArrayField(TEXT("reagents"), Reagents))
		{
			for (const TSharedPtr<FJsonValue>& R : *Reagents)
			{
				if (const TSharedPtr<FJsonObject> RO = R->AsObject())
				{
					D.Reagents.Add({FName(*Str(*RO, TEXT("class"))), Int(*RO, TEXT("count"))});
				}
			}
		}
		Spells.Add(D.Class, MoveTemp(D));
	}

	for (const TSharedPtr<FJsonValue>& V : LoadArray(TEXT("skills.json")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		FMRSkillDef D;
		D.Class = FName(*Str(*O, TEXT("class")));
		D.Name = Capitalised(Str(*O, TEXT("vrName")));
		D.Desc = Desc(Str(*O, TEXT("vrDesc")));
		D.Icon = IconName(Str(*O, TEXT("vrIcon")));
		D.School = Int(*O, TEXT("viSchool"));
		D.Level = Int(*O, TEXT("viSkill_level"));
		Skills.Add(D.Class, MoveTemp(D));
	}
	UE_LOG(LogMeridian, Log, TEXT("UI data: %d items, %d spells, %d skills"), Items.Num(), Spells.Num(), Skills.Num());
}

EMREquipSlot UMRGameDataSubsystem::EquipSlotFor(const FMRItemDef& Item) const
{
	using namespace MRItemUse;
	const int32 U = Item.UseType;
	if (U & (Head | Face)) return EMREquipSlot::Head;
	if (U & Neck) return EMREquipSlot::Amulet;
	if (U & (Body | Shirt)) return EMREquipSlot::Torso;  // robes are Body | Legs: the torso
	if (U & Gauntlet) return EMREquipSlot::Hands;
	if (U & Legs) return EMREquipSlot::Legs;
	if (U & Finger) return EMREquipSlot::Ring1;
	if (U & Hand)
	{
		// weapons are wielded from the hotbar (the right hand); shields and torches go in the left
		return Item.Kind == TEXT("Weapon") ? EMREquipSlot::RightHand : EMREquipSlot::LeftHand;
	}
	return EMREquipSlot::Count;
}

bool UMRGameDataSubsystem::FitsEquipSlot(const FMRItemDef& Item, EMREquipSlot Slot) const
{
	if (Slot == EMREquipSlot::RightHand)
	{
		return true;
	}
	const EMREquipSlot Fit = EquipSlotFor(Item);
	return Fit == Slot || (Fit == EMREquipSlot::Ring1 && Slot == EMREquipSlot::Ring2);
}

FText UMRGameDataSubsystem::SchoolName(int32 School)
{
	switch (School)
	{
	case 1: return NSLOCTEXT("MR", "Shalille", "Shal'ille");
	case 2: return NSLOCTEXT("MR", "Qor", "Qor");
	case 3: return NSLOCTEXT("MR", "Kraanan", "Kraanan");
	case 4: return NSLOCTEXT("MR", "Faren", "Faren");
	case 5: return NSLOCTEXT("MR", "Riija", "Riija");
	case 6: return NSLOCTEXT("MR", "Jala", "Jala");
	case 7: return NSLOCTEXT("MR", "Crafting", "Crafting");
	case 10: return NSLOCTEXT("MR", "Weaponcraft", "Weaponcraft");
	case 11: return NSLOCTEXT("MR", "Brawling", "Brawling");
	default: return NSLOCTEXT("MR", "Other", "Other");
	}
}
