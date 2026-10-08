#include "UI/MRInventorySource.h"

#include "Dom/JsonObject.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MRGameData.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	constexpr int32 HotbarSlots = 9;
	constexpr int32 BagColumns = 9;
	constexpr int32 MinBagRows = 4;
	constexpr int32 MaxNumberStack = 9999;  // Kod NumberItems have no limit; a slot has to stop somewhere
	const FMRSlotRef CursorSlot(EMRSlotArea::Cursor, 0);

	const TCHAR* EquipNames[] = {TEXT("Head"), TEXT("Amulet"), TEXT("Torso"), TEXT("Hands"), TEXT("Legs"),
		TEXT("Ring1"), TEXT("Ring2"), TEXT("LeftHand"), TEXT("RightHand")};
	static_assert(UE_ARRAY_COUNT(EquipNames) == static_cast<int32>(EMREquipSlot::Count), "EquipNames");
}

// ------------------------------------------------------------------------------ queries

int32 UMRInventorySource::NumSlots(EMRSlotArea Area) const
{
	switch (Area)
	{
	case EMRSlotArea::Hotbar:
	case EMRSlotArea::SpellBar:
		return HotbarSlots;
	case EMRSlotArea::Equipment:
		return static_cast<int32>(EMREquipSlot::Count);
	case EMRSlotArea::SpellBook:
		return GetKnownSpells().Num();
	case EMRSlotArea::Cursor:
		return 1;
	case EMRSlotArea::Bag:
	{
		// the used rows plus one free row, at least MinBagRows
		int32 Last = -1;
		for (int32 i = 0; i < 4096; ++i)
		{
			if (!GetRaw(FMRSlotRef(EMRSlotArea::Bag, i)).IsEmpty())
			{
				Last = i;
			}
			else if (i > Last + BagColumns * 2)
			{
				break;
			}
		}
		const int32 Rows = FMath::Max(MinBagRows, (Last + 1 + BagColumns - 1) / BagColumns + 1);
		return Rows * BagColumns;
	}
	default:
		return 0;
	}
}

FMRSlotRef UMRInventorySource::Resolve(const FMRSlotRef& Slot) const
{
	if (Slot.Area == EMRSlotArea::Equipment && Slot.Index == static_cast<int32>(EMREquipSlot::RightHand))
	{
		return FMRSlotRef(EMRSlotArea::Hotbar, SelectedHotbar);
	}
	return Slot;
}

FMRSlotContent UMRInventorySource::Get(const FMRSlotRef& Slot) const
{
	const FMRSlotRef S = Resolve(Slot);
	if (S.Area == EMRSlotArea::SpellBook)
	{
		const TArray<FName>& Known = GetKnownSpells();
		return Known.IsValidIndex(S.Index) ? FMRSlotContent::Spell(Known[S.Index]) : FMRSlotContent();
	}
	return GetRaw(S);
}

bool UMRInventorySource::Accepts(const FMRSlotRef& Slot, const FMRSlotContent& Content) const
{
	if (Content.IsEmpty())
	{
		return true;
	}
	const FMRSlotRef S = Resolve(Slot);
	switch (S.Area)
	{
	case EMRSlotArea::Bag:
	case EMRSlotArea::Hotbar:
		return !Content.bSpell;
	case EMRSlotArea::Cursor:
		return true;
	case EMRSlotArea::SpellBar:
		return Content.bSpell;
	case EMRSlotArea::Equipment:
	{
		if (Content.bSpell || !Data)
		{
			return false;
		}
		const FMRItemDef* Item = Data->FindItem(Content.Id);
		return Item && Data->FitsEquipSlot(*Item, static_cast<EMREquipSlot>(S.Index));
	}
	default:
		return false;
	}
}

int32 UMRInventorySource::MaxStack(const FMRSlotRef& Slot, const FMRSlotContent& Content) const
{
	if (Content.bSpell || Resolve(Slot).Area == EMRSlotArea::Equipment)
	{
		return 1;
	}
	const FMRItemDef* Item = Data ? Data->FindItem(Content.Id) : nullptr;
	return Item && Item->bStackable ? MaxNumberStack : 1;
}

void UMRInventorySource::GetTotals(int32& OutWeight, int32& OutBulk) const
{
	OutWeight = OutBulk = 0;
	auto Add = [&](const FMRSlotContent& C)
	{
		const FMRItemDef* Item = !C.IsEmpty() && !C.bSpell && Data ? Data->FindItem(C.Id) : nullptr;
		if (Item)
		{
			OutWeight += Item->Weight * C.Count;
			OutBulk += Item->Bulk * C.Count;
		}
	};
	const int32 BagSlots = NumSlots(EMRSlotArea::Bag);
	for (int32 i = 0; i < BagSlots; ++i)
	{
		Add(GetRaw(FMRSlotRef(EMRSlotArea::Bag, i)));
	}
	for (int32 i = 0; i < HotbarSlots; ++i)
	{
		Add(GetRaw(FMRSlotRef(EMRSlotArea::Hotbar, i)));
	}
	for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::RightHand); ++i)
	{
		Add(GetRaw(FMRSlotRef(EMRSlotArea::Equipment, i)));
	}
	Add(GetRaw(CursorSlot));
}

// ------------------------------------------------------------------------------ interactions

void UMRInventorySource::Click(const FMRSlotRef& Slot, bool bRight)
{
	const FMRSlotRef S = Resolve(Slot);
	FMRSlotContent Cur = GetRaw(CursorSlot);
	const FMRSlotContent In = Get(S);

	if (S.Area == EMRSlotArea::SpellBook)
	{
		// read only: picking a spell up copies it; anything carried is let go (spells) or kept
		if (Cur.IsEmpty() && !In.IsEmpty())
		{
			SetRaw(CursorSlot, In);
			CursorOrigin = FMRSlotRef();
			Changed();
		}
		else if (Cur.bSpell)
		{
			SetRaw(CursorSlot, FMRSlotContent());
			Changed();
		}
		return;
	}

	if (Cur.IsEmpty())
	{
		if (In.IsEmpty())
		{
			return;
		}
		const int32 Take = bRight && !In.bSpell ? (In.Count + 1) / 2 : In.Count;
		SetRaw(CursorSlot, FMRSlotContent(In.Id, Take, In.bSpell));
		SetRaw(S, In.Count - Take > 0 ? FMRSlotContent(In.Id, In.Count - Take, In.bSpell) : FMRSlotContent());
		CursorOrigin = S;
		Changed();
		return;
	}

	if (!Accepts(S, Cur))
	{
		if (Cur.bSpell)
		{
			SetRaw(CursorSlot, FMRSlotContent());  // a spell only lives on the spell bar
			Changed();
		}
		return;
	}

	if (In.IsEmpty() || In.SameThing(Cur))
	{
		if (Cur.bSpell)
		{
			SetRaw(S, Cur);
			SetRaw(CursorSlot, FMRSlotContent());
			Changed();
			return;
		}
		const int32 Room = MaxStack(S, Cur) - (In.IsEmpty() ? 0 : In.Count);
		const int32 Put = FMath::Min(bRight ? 1 : Cur.Count, Room);
		if (Put <= 0)
		{
			return;
		}
		SetRaw(S, FMRSlotContent(Cur.Id, (In.IsEmpty() ? 0 : In.Count) + Put));
		Cur.Count -= Put;
		SetRaw(CursorSlot, Cur.Count > 0 ? Cur : FMRSlotContent());
		Changed();
		return;
	}

	// a different thing: swap, if the carried stack fits the slot whole
	if (Cur.Count > MaxStack(S, Cur))
	{
		return;
	}
	SetRaw(S, Cur);
	SetRaw(CursorSlot, In);
	CursorOrigin = S;
	Changed();
}

FMRSlotContent UMRInventorySource::Insert(EMRSlotArea Area, FMRSlotContent Content)
{
	if (Content.IsEmpty())
	{
		return Content;
	}
	const int32 N = NumSlots(Area);
	// merge onto stacks of the same item first, then fill free slots
	for (int32 Pass = 0; Pass < 2 && Content.Count > 0; ++Pass)
	{
		for (int32 i = 0; i < N && Content.Count > 0; ++i)
		{
			const FMRSlotRef S(Area, i);
			const FMRSlotContent In = GetRaw(S);
			if (!Accepts(S, Content) || (Pass == 0 ? !In.SameThing(Content) || In.IsEmpty() : !In.IsEmpty()))
			{
				continue;
			}
			const int32 Have = In.IsEmpty() ? 0 : In.Count;
			const int32 Put = FMath::Min(Content.Count, MaxStack(S, Content) - Have);
			if (Put > 0)
			{
				SetRaw(S, FMRSlotContent(Content.Id, Have + Put, Content.bSpell));
				Content.Count -= Put;
			}
		}
	}
	return Content.Count > 0 ? Content : FMRSlotContent();
}

void UMRInventorySource::QuickMove(const FMRSlotRef& Slot)
{
	const FMRSlotRef S = Resolve(Slot);
	const FMRSlotContent In = Get(S);
	if (In.IsEmpty())
	{
		return;
	}
	switch (S.Area)
	{
	case EMRSlotArea::SpellBook:
		for (int32 i = 0; i < HotbarSlots; ++i)
		{
			const FMRSlotRef B(EMRSlotArea::SpellBar, i);
			if (GetRaw(B).SameThing(In))
			{
				return;  // already on the bar
			}
		}
		Insert(EMRSlotArea::SpellBar, In);
		break;
	case EMRSlotArea::SpellBar:
		SetRaw(S, FMRSlotContent());
		break;
	case EMRSlotArea::Bag:
	{
		// equip it if its slot is free (the first free ring slot for rings), else to the hotbar
		const FMRItemDef* Item = Data ? Data->FindItem(In.Id) : nullptr;
		const EMREquipSlot Fit = Item ? Data->EquipSlotFor(*Item) : EMREquipSlot::Count;
		if (Fit != EMREquipSlot::Count && Fit != EMREquipSlot::RightHand)
		{
			for (EMREquipSlot E : {Fit, Fit == EMREquipSlot::Ring1 ? EMREquipSlot::Ring2 : Fit})
			{
				const FMRSlotRef Target = FMRSlotRef::Equip(E);
				if (GetRaw(Target).IsEmpty())
				{
					SetRaw(Target, FMRSlotContent(In.Id, 1));
					SetRaw(S, In.Count > 1 ? FMRSlotContent(In.Id, In.Count - 1) : FMRSlotContent());
					Changed();
					return;
				}
			}
		}
		SetRaw(S, Insert(EMRSlotArea::Hotbar, In));
		break;
	}
	case EMRSlotArea::Hotbar:
	case EMRSlotArea::Equipment:
		SetRaw(S, FMRSlotContent());
		SetRaw(S, Insert(EMRSlotArea::Bag, In));
		break;
	default:
		return;
	}
	Changed();
}

void UMRInventorySource::SwapWithHotbar(const FMRSlotRef& Slot, int32 HotbarIndex)
{
	const FMRSlotRef S = Resolve(Slot);
	const FMRSlotRef H(EMRSlotArea::Hotbar, FMath::Clamp(HotbarIndex, 0, HotbarSlots - 1));
	if (S == H || S.Area == EMRSlotArea::SpellBook || S.Area == EMRSlotArea::SpellBar)
	{
		return;
	}
	const FMRSlotContent A = GetRaw(S);
	const FMRSlotContent B = GetRaw(H);
	if (!Accepts(S, B) || !Accepts(H, A) || (!B.IsEmpty() && B.Count > MaxStack(S, B)))
	{
		return;
	}
	SetRaw(S, B);
	SetRaw(H, A);
	Changed();
}

void UMRInventorySource::DropCursor(bool bOne)
{
	FMRSlotContent Cur = GetRaw(CursorSlot);
	if (Cur.IsEmpty())
	{
		return;
	}
	if (Cur.bSpell)
	{
		SetRaw(CursorSlot, FMRSlotContent());
		Changed();
		return;
	}
	const int32 N = bOne ? 1 : Cur.Count;
	OnDropped(FMRSlotContent(Cur.Id, N));
	Cur.Count -= N;
	SetRaw(CursorSlot, Cur.Count > 0 ? Cur : FMRSlotContent());
	Changed();
}

void UMRInventorySource::ReturnCursor()
{
	FMRSlotContent Cur = GetRaw(CursorSlot);
	if (Cur.IsEmpty())
	{
		return;
	}
	SetRaw(CursorSlot, FMRSlotContent());
	if (!Cur.bSpell)
	{
		if (CursorOrigin.IsValid() && CursorOrigin.Area != EMRSlotArea::SpellBook && Accepts(CursorOrigin, Cur))
		{
			const FMRSlotContent In = GetRaw(CursorOrigin);
			if (In.IsEmpty() || In.SameThing(Cur))
			{
				const int32 Have = In.IsEmpty() ? 0 : In.Count;
				const int32 Put = FMath::Min(Cur.Count, MaxStack(CursorOrigin, Cur) - Have);
				SetRaw(CursorOrigin, FMRSlotContent(Cur.Id, Have + Put));
				Cur.Count -= Put;
			}
		}
		if (Cur.Count > 0)
		{
			Insert(EMRSlotArea::Bag, Cur);
		}
	}
	CursorOrigin = FMRSlotRef();
	Changed();
}

void UMRInventorySource::SelectHotbar(int32 Index)
{
	const int32 New = ((Index % HotbarSlots) + HotbarSlots) % HotbarSlots;
	if (New == SelectedHotbar)
	{
		return;
	}
	SelectedHotbar = New;
	OnSelectionChanged.Broadcast();
	Changed();  // the right hand shows the new selection
}

void UMRInventorySource::OnDropped(const FMRSlotContent& Content)
{
}

// ------------------------------------------------------------------------------ mock

void UMRMockInventory::LoadFromJson()
{
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ui"), TEXT("mock_inventory.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogMeridian, Warning, TEXT("UI: %s not found; the inventory starts empty"), *Path);
		return;
	}
	auto ReadStack = [this](const TSharedPtr<FJsonValue>& V) -> FMRSlotContent
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		FString Item;
		if (!O || !O->TryGetStringField(TEXT("item"), Item))
		{
			return FMRSlotContent();
		}
		if (Data && !Data->FindItem(FName(*Item)))
		{
			UE_LOG(LogMeridian, Warning, TEXT("UI mock inventory: %s is not in items.json"), *Item);
			return FMRSlotContent();
		}
		int32 Count = 1;
		O->TryGetNumberField(TEXT("count"), Count);
		return FMRSlotContent(FName(*Item), FMath::Max(1, Count));
	};

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("hotbar"), Arr))
	{
		for (int32 i = 0; i < Arr->Num() && i < HotbarSlots; ++i)
		{
			Hotbar[i] = ReadStack((*Arr)[i]);
		}
	}
	if (Root->TryGetArrayField(TEXT("bag"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const FMRSlotContent C = ReadStack(V);
			if (!C.IsEmpty())
			{
				Bag.Add(C);
			}
		}
	}
	const TSharedPtr<FJsonObject>* Equip = nullptr;
	if (Root->TryGetObjectField(TEXT("equipment"), Equip))
	{
		for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
		{
			if (const TSharedPtr<FJsonValue> V = (*Equip)->TryGetField(EquipNames[i]))
			{
				Equipment[i] = ReadStack(V);
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("spells"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const FName Spell(*V->AsString());
			if (!Data || Data->FindSpell(Spell))
			{
				KnownSpells.Add(Spell);
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("spell_bar"), Arr))
	{
		for (int32 i = 0; i < Arr->Num() && i < HotbarSlots; ++i)
		{
			FString Spell;
			if ((*Arr)[i].IsValid() && (*Arr)[i]->TryGetString(Spell))
			{
				SpellBar[i] = FMRSlotContent::Spell(FName(*Spell));
			}
		}
	}
	const TSharedPtr<FJsonObject>* SkillObj = nullptr;
	if (Root->TryGetObjectField(TEXT("skills"), SkillObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*SkillObj)->Values)
		{
			Skills.Add(FName(*P.Key), static_cast<int32>(P.Value->AsNumber()));
		}
	}
	if (Root->TryGetArrayField(TEXT("stats"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O)
			{
				continue;
			}
			FMRStatView S;
			O->TryGetStringField(TEXT("name"), S.Name);
			O->TryGetNumberField(TEXT("value"), S.Value);
			O->TryGetNumberField(TEXT("min"), S.Min);
			O->TryGetNumberField(TEXT("max"), S.Max);
			Stats.Add(S);
		}
	}
	Changed();
}

void UMRMockInventory::SetKnownSpells(const TArray<FName>& InSpells, const TMap<FName, int32>& InPercents)
{
	KnownSpells = InSpells;
	SpellPercents = InPercents;
	Changed();
}

void UMRMockInventory::SetSkills(const TMap<FName, int32>& InSkills)
{
	Skills = InSkills;
	Changed();
}

FMRSlotContent UMRMockInventory::GetRaw(const FMRSlotRef& Slot) const
{
	switch (Slot.Area)
	{
	case EMRSlotArea::Bag:
		return Bag.IsValidIndex(Slot.Index) ? Bag[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::Hotbar:
		return Slot.Index >= 0 && Slot.Index < HotbarSlots ? Hotbar[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::Equipment:
		return Slot.Index >= 0 && Slot.Index < static_cast<int32>(EMREquipSlot::Count) ? Equipment[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::SpellBar:
		return Slot.Index >= 0 && Slot.Index < HotbarSlots ? SpellBar[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::Cursor:
		return Cursor;
	default:
		return FMRSlotContent();
	}
}

void UMRMockInventory::SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content)
{
	const FMRSlotContent C = Content.IsEmpty() ? FMRSlotContent() : Content;
	switch (Slot.Area)
	{
	case EMRSlotArea::Bag:
		if (Slot.Index >= 0 && Slot.Index < 4096)
		{
			if (Slot.Index >= Bag.Num())
			{
				if (C.IsEmpty())
				{
					return;
				}
				Bag.SetNum(Slot.Index + 1);
			}
			Bag[Slot.Index] = C;
			while (Bag.Num() > 0 && Bag.Last().IsEmpty())
			{
				Bag.Pop();
			}
		}
		break;
	case EMRSlotArea::Hotbar:
		if (Slot.Index >= 0 && Slot.Index < HotbarSlots)
		{
			Hotbar[Slot.Index] = C;
		}
		break;
	case EMRSlotArea::Equipment:
		if (Slot.Index >= 0 && Slot.Index < static_cast<int32>(EMREquipSlot::Count))
		{
			Equipment[Slot.Index] = C;
		}
		break;
	case EMRSlotArea::SpellBar:
		if (Slot.Index >= 0 && Slot.Index < HotbarSlots)
		{
			SpellBar[Slot.Index] = C;
		}
		break;
	case EMRSlotArea::Cursor:
		Cursor = C;
		break;
	default:
		break;
	}
}

void UMRMockInventory::OnDropped(const FMRSlotContent& Content)
{
	// nothing to drop into yet: the server will create the item in the world
	UE_LOG(LogMeridian, Log, TEXT("UI mock inventory: dropped %d x %s"), Content.Count, *Content.Id.ToString());
}
