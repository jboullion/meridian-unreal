#include "UI/MRInventorySource.h"

#include "Dom/JsonObject.h"
#include "UnrealMeridian.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MRGameData.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	constexpr int32 BagColumns = 9;
	constexpr int32 MinBagRows = 4;
	constexpr int32 MaxNumberStack = 9999;  // Kod NumberItems have no limit; a slot has to stop somewhere
	const FMRSlotRef CursorSlot(EMRSlotArea::Cursor, 0);

	const TCHAR* EquipNames[] = {TEXT("Head"), TEXT("Amulet"), TEXT("Torso"), TEXT("Hands"), TEXT("Legs"),
		TEXT("Ring1"), TEXT("Ring2"), TEXT("LeftHand"), TEXT("RightHand")};
	static_assert(UE_ARRAY_COUNT(EquipNames) == static_cast<int32>(EMREquipSlot::Count), "EquipNames");

	bool ValidQuick(int32 Index) { return Index >= 0 && Index < FMRQuickSlot::Num; }
}

// ------------------------------------------------------------------------------ quick slot text

FString FMRQuickSlot::ToString() const
{
	if (IsEmpty())
	{
		return FString();
	}
	return bSpell ? FString::Printf(TEXT("spell|%s"), *Id.ToString())
		: FString::Printf(TEXT("item|%s|%s|%s"), *Id.ToString(), *Icon, *Name);
}

FMRQuickSlot FMRQuickSlot::FromString(const FString& Text)
{
	FMRQuickSlot Q;
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("|"), false);
	if (Parts.Num() >= 2 && Parts[0] == TEXT("spell") && !Parts[1].IsEmpty())
	{
		Q.bSpell = true;
		Q.Id = FName(*Parts[1]);
	}
	else if (Parts.Num() >= 4 && Parts[0] == TEXT("item") && !Parts[1].IsEmpty())
	{
		Q.Id = FName(*Parts[1]);
		Q.Icon = Parts[2];
		// (a name with a "|" in it: the rest of the line)
		Q.Name = FString::Join(TArrayView<const FString>(Parts).RightChop(3), TEXT("|"));
	}
	return Q;
}

// ------------------------------------------------------------------------------ queries

int32 UMRInventorySource::NumSlots(EMRSlotArea Area) const
{
	switch (Area)
	{
	case EMRSlotArea::QuickSlot:
		return FMRQuickSlot::Num;
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
	// (until 2026-10-10 the right hand was the selected hotbar slot, as Minecraft's; it is a slot of
	// its own now, offline as online)
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
	if (S.Area == EMRSlotArea::QuickSlot)
	{
		// what the slot names: its spell, or the item it stands for now (how many are carried); named
		// but not carried, the item by its class (drawn greyed: IsQuickSlotAvailable)
		const FMRQuickSlot& Q = GetQuickSlot(S.Index);
		if (Q.IsEmpty())
		{
			return FMRSlotContent();
		}
		if (Q.bSpell)
		{
			return FMRSlotContent::Spell(Q.Id);
		}
		const FQuickItem Item = FindQuickItem(Q);
		return Item.Content.IsEmpty() ? FMRSlotContent(Q.Id, 1) : Item.Content;
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
		return !Content.bSpell;
	case EMRSlotArea::Cursor:
	case EMRSlotArea::QuickSlot:
		return true;
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
	for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
	{
		Add(GetRaw(FMRSlotRef(EMRSlotArea::Equipment, i)));
	}
	Add(GetRaw(CursorSlot));
}

// ------------------------------------------------------------------------------ quick slots

const FMRQuickSlot& UMRInventorySource::GetQuickSlot(int32 Index) const
{
	static const FMRQuickSlot None;
	return ValidQuick(Index) ? QuickSlots[Index] : None;
}

void UMRInventorySource::SetQuickSlot(int32 Index, const FMRQuickSlot& Slot)
{
	if (!ValidQuick(Index) || QuickSlots[Index] == Slot)
	{
		return;
	}
	QuickSlots[Index] = Slot;
	OnQuickSlotsChanged();
	Changed();
}

void UMRInventorySource::SwapQuickSlots(int32 A, int32 B)
{
	if (!ValidQuick(A) || !ValidQuick(B) || A == B)
	{
		return;
	}
	Swap(QuickSlots[A], QuickSlots[B]);
	OnQuickSlotsChanged();
	Changed();
}

FMRQuickSlot UMRInventorySource::QuickSlotFor(const FMRSlotContent& Content) const
{
	FMRQuickSlot Q;
	if (Content.IsEmpty())
	{
		return Q;
	}
	Q.bSpell = Content.bSpell;
	Q.Id = Content.Id;
	if (!Content.bSpell)
	{
		const FMRItemDef* Item = Data ? Data->FindItem(Content.Id) : nullptr;
		Q.Icon = Item ? Item->Icon.ToString() + TEXT(".bgf") : Content.Id.ToString() + TEXT(".bgf");
		Q.Name = Item ? Item->Name.ToString() : Content.Id.ToString();
	}
	return Q;
}

UMRInventorySource::FQuickItem UMRInventorySource::FindQuickItem(const FMRQuickSlot& Slot) const
{
	// offline: by class, the equipment first (in use), then the bag
	FQuickItem Found;
	if (Slot.IsEmpty() || Slot.bSpell)
	{
		return Found;
	}
	int32 Count = 0;
	for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
	{
		const FMRSlotContent C = GetRaw(FMRSlotRef(EMRSlotArea::Equipment, i));
		if (!C.IsEmpty() && C.Id == Slot.Id)
		{
			Count += C.Count;
			if (Found.Content.IsEmpty())
			{
				Found.Content = C;
				Found.bInUse = true;
			}
		}
	}
	const int32 BagSlots = NumSlots(EMRSlotArea::Bag);
	for (int32 i = 0; i < BagSlots; ++i)
	{
		const FMRSlotContent C = GetRaw(FMRSlotRef(EMRSlotArea::Bag, i));
		if (!C.IsEmpty() && C.Id == Slot.Id)
		{
			Count += C.Count;
			if (Found.Content.IsEmpty())
			{
				Found.Content = C;
			}
		}
	}
	Found.Content.Count = Count;
	return Found;
}

bool UMRInventorySource::IsQuickSlotAvailable(int32 Index) const
{
	const FMRQuickSlot& Q = GetQuickSlot(Index);
	if (Q.IsEmpty())
	{
		return false;
	}
	return Q.bSpell ? GetKnownSpells().Contains(Q.Id) : !FindQuickItem(Q).Content.IsEmpty();
}

bool UMRInventorySource::IsQuickSlotInUse(int32 Index) const
{
	const FMRQuickSlot& Q = GetQuickSlot(Index);
	return !Q.IsEmpty() && !Q.bSpell && FindQuickItem(Q).bInUse;
}

void UMRInventorySource::AssignQuickSlot(const FMRSlotRef& Slot, int32 Index)
{
	const FMRSlotRef S = Resolve(Slot);
	if (S.Area == EMRSlotArea::QuickSlot)
	{
		SwapQuickSlots(S.Index, Index);
		return;
	}
	const FMRSlotContent In = Get(S);
	if (!In.IsEmpty() && S.Area != EMRSlotArea::Cursor)
	{
		SetQuickSlot(Index, QuickSlotFor(In));
	}
}

void UMRInventorySource::UseQuickItem(int32 Index)
{
	// offline: put it on in its own slot (what was there goes to the bag), or take it off
	const FMRQuickSlot& Q = GetQuickSlot(Index);
	if (Q.IsEmpty() || Q.bSpell)
	{
		return;
	}
	for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
	{
		const FMRSlotRef E(EMRSlotArea::Equipment, i);
		if (GetRaw(E).Id == Q.Id)
		{
			QuickMove(E);  // on already: off to the bag
			return;
		}
	}
	const int32 BagSlots = NumSlots(EMRSlotArea::Bag);
	for (int32 i = 0; i < BagSlots; ++i)
	{
		const FMRSlotRef B(EMRSlotArea::Bag, i);
		const FMRSlotContent In = GetRaw(B);
		if (In.IsEmpty() || In.Id != Q.Id)
		{
			continue;
		}
		const FMRItemDef* Item = Data ? Data->FindItem(In.Id) : nullptr;
		EMREquipSlot Fit = Item ? Data->EquipSlotFor(*Item) : EMREquipSlot::Count;
		if (Fit == EMREquipSlot::Count)
		{
			return;  // nothing to put on (eating and reading come with the server)
		}
		if (Fit == EMREquipSlot::Ring1 && !GetRaw(FMRSlotRef::Equip(EMREquipSlot::Ring1)).IsEmpty()
			&& GetRaw(FMRSlotRef::Equip(EMREquipSlot::Ring2)).IsEmpty())
		{
			Fit = EMREquipSlot::Ring2;
		}
		const FMRSlotRef E = FMRSlotRef::Equip(Fit);
		const FMRSlotContent Was = GetRaw(E);
		SetRaw(E, FMRSlotContent(In.Id, 1));
		SetRaw(B, In.Count > 1 ? FMRSlotContent(In.Id, In.Count - 1) : FMRSlotContent());
		if (!Was.IsEmpty())
		{
			Insert(EMRSlotArea::Bag, Was);
		}
		Changed();
		return;
	}
}

void UMRInventorySource::LoadQuickSlots(const TArray<FString>& Saved)
{
	for (int32 i = 0; i < FMRQuickSlot::Num; ++i)
	{
		QuickSlots[i] = Saved.IsValidIndex(i) ? FMRQuickSlot::FromString(Saved[i]) : FMRQuickSlot();
	}
}

void UMRInventorySource::SaveQuickSlots(TArray<FString>& Out) const
{
	Out.SetNum(FMRQuickSlot::Num);
	for (int32 i = 0; i < FMRQuickSlot::Num; ++i)
	{
		Out[i] = QuickSlots[i].ToString();
	}
	// (empty slots at the end aren't kept)
	while (Out.Num() > 0 && Out.Last().IsEmpty())
	{
		Out.Pop();
	}
}

// ------------------------------------------------------------------------------ interactions

void UMRInventorySource::Click(const FMRSlotRef& Slot, bool bRight)
{
	const FMRSlotRef S = Resolve(Slot);
	FMRSlotContent Cur = GetRaw(CursorSlot);
	const FMRSlotContent In = Get(S);

	if (S.Area == EMRSlotArea::QuickSlot)
	{
		// carrying something: name it here, and the stack goes back where it came from (a spell is let go)
		if (!Cur.IsEmpty())
		{
			SetQuickSlot(S.Index, QuickSlotFor(Cur));
			ReturnCursor();
		}
		return;
	}

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
			SetRaw(CursorSlot, FMRSlotContent());  // a spell only goes on a quick slot
			Changed();
		}
		return;
	}

	if (In.IsEmpty() || In.SameThing(Cur))
	{
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

bool UMRInventorySource::EquipCarried()
{
	const FMRSlotContent Cur = Get(CursorSlot);
	if (Cur.IsEmpty() || Cur.bSpell)
	{
		return false;
	}
	for (const bool bFreeOnly : {true, false})
	{
		for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
		{
			const FMRSlotRef E = FMRSlotRef::Equip(static_cast<EMREquipSlot>(i));
			if (Accepts(E, Cur) && (!bFreeOnly || Get(E).IsEmpty()))
			{
				Click(E, false);  // the slot's own rules (online: the server uses it)
				return Get(CursorSlot).IsEmpty();
			}
		}
	}
	return false;
}

void UMRInventorySource::QuickMove(const FMRSlotRef& Slot)
{
	const FMRSlotRef S = Resolve(Slot);
	const FMRSlotContent In = Get(S);
	if (In.IsEmpty())
	{
		return;
	}
	// name it on the first free quick slot (the numpad row first for a spell, as the spell bar was), unless one names it already
	auto ToQuickSlot = [this](const FMRSlotContent& C)
	{
		const FMRQuickSlot Q = QuickSlotFor(C);
		for (int32 i = 0; i < FMRQuickSlot::Num; ++i)
		{
			if (QuickSlots[i] == Q)
			{
				return;
			}
		}
		for (int32 n = 0; n < FMRQuickSlot::Num; ++n)
		{
			const int32 i = C.bSpell ? (n + FMRQuickSlot::PerRow) % FMRQuickSlot::Num : n;
			if (QuickSlots[i].IsEmpty())
			{
				SetQuickSlot(i, Q);
				return;
			}
		}
	};
	switch (S.Area)
	{
	case EMRSlotArea::SpellBook:
		ToQuickSlot(In);
		return;
	case EMRSlotArea::QuickSlot:
		SetQuickSlot(S.Index, FMRQuickSlot());
		return;
	case EMRSlotArea::Bag:
	{
		// equip it if its slot is free (the first free ring slot for rings), else name it on a quick slot
		const FMRItemDef* Item = Data ? Data->FindItem(In.Id) : nullptr;
		const EMREquipSlot Fit = Item ? Data->EquipSlotFor(*Item) : EMREquipSlot::Count;
		if (Fit != EMREquipSlot::Count)
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
		ToQuickSlot(In);
		return;
	}
	case EMRSlotArea::Equipment:
		SetRaw(S, FMRSlotContent());
		SetRaw(S, Insert(EMRSlotArea::Bag, In));
		break;
	default:
		return;
	}
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
	// the quick slots: "quick_slots" [{"item": ...} or {"spell": ...} or null], row 0 (1-0) then row 1 (the numpad)
	if (Root->TryGetArrayField(TEXT("quick_slots"), Arr))
	{
		for (int32 i = 0; i < Arr->Num() && i < FMRQuickSlot::Num; ++i)
		{
			const TSharedPtr<FJsonObject> O = (*Arr)[i].IsValid() ? (*Arr)[i]->AsObject() : nullptr;
			FString Name;
			if (O && O->TryGetStringField(TEXT("spell"), Name))
			{
				QuickSlots[i] = QuickSlotFor(FMRSlotContent::Spell(FName(*Name)));
			}
			else if (O && O->TryGetStringField(TEXT("item"), Name))
			{
				QuickSlots[i] = QuickSlotFor(FMRSlotContent(FName(*Name), 1));
			}
		}
	}
	const TSharedPtr<FJsonObject>* SkillObj = nullptr;
	if (Root->TryGetObjectField(TEXT("skills"), SkillObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>> P : (*SkillObj)->Values)
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

FMRSlotContent UMRMockInventory::GetRaw(const FMRSlotRef& Slot) const
{
	switch (Slot.Area)
	{
	case EMRSlotArea::Bag:
		return Bag.IsValidIndex(Slot.Index) ? Bag[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::Equipment:
		return Slot.Index >= 0 && Slot.Index < static_cast<int32>(EMREquipSlot::Count) ? Equipment[Slot.Index] : FMRSlotContent();
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
	case EMRSlotArea::Equipment:
		if (Slot.Index >= 0 && Slot.Index < static_cast<int32>(EMREquipSlot::Count))
		{
			Equipment[Slot.Index] = C;
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

// ------------------------------------------------------------------------------ online

void UMRNetInventory::SetKnownSpells(const TArray<FName>& InSpells, const TMap<FName, int32>& InPercents)
{
	// (a quick slot keeps a spell the character no longer knows, greyed, as Shards does)
	KnownSpells = InSpells;
	SpellPercents = InPercents;
	Changed();
}

void UMRNetInventory::SetSkills(const TMap<FName, int32>& InSkills)
{
	Skills = InSkills;
	Changed();
}

FMRSlotContent UMRNetInventory::GetRaw(const FMRSlotRef& Slot) const
{
	switch (Slot.Area)
	{
	case EMRSlotArea::Cursor:
		return Cursor;
	case EMRSlotArea::Bag:
		return BagView.IsValidIndex(Slot.Index) ? BagView[Slot.Index] : FMRSlotContent();
	case EMRSlotArea::Equipment:
		return Slot.Index >= 0 && Slot.Index < static_cast<int32>(EMREquipSlot::Count) ? EquipView[Slot.Index] : FMRSlotContent();
	default:
		return FMRSlotContent();
	}
}

void UMRNetInventory::SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content)
{
	// only what lives on this client: the cursor (spells: the base class's rules)
	if (Slot.Area == EMRSlotArea::Cursor)
	{
		Cursor = Content.IsEmpty() ? FMRSlotContent() : Content;
	}
}

// ------------------------------------------------------------------------------ online items

void UMRNetInventory::SetNet(UMRNetSubsystem* InNet)
{
	if (UMRNetSubsystem* Old = Net.Get())
	{
		Old->OnInventoryChanged.Remove(InventoryHandle);
		Old->OnCharacterDataLoaded.Remove(LayoutHandle);
	}
	Net = InNet;
	if (InNet)
	{
		InventoryHandle = InNet->OnInventoryChanged.AddUObject(this, &UMRNetInventory::Rebuild);
		LayoutHandle = InNet->OnCharacterDataLoaded.AddUObject(this, &UMRNetInventory::LoadLayout);
	}
	LoadLayout();  // (a new source for a character already read: entering the game again)
}

void UMRNetInventory::LoadLayout()
{
	UMRNetSubsystem* N = Net.Get();
	if (!N)
	{
		Rebuild();
		return;
	}
	FMRSocial& S = N->GetSocial();
	LoadQuickSlots(S.QuickSlots);
	// kept before 2026-10-14: the item hotbar (icon|name, keys 1-9) to row 0, the spell bar (numpad) to row 1
	if (S.Hotbar.Num() > 0 || S.SpellBar.Num() > 0)
	{
		for (int32 i = 0; i < 9; ++i)
		{
			FString Icon, Name;
			if (S.Hotbar.IsValidIndex(i) && S.Hotbar[i].Split(TEXT("|"), &Icon, &Name) && QuickSlots[i].IsEmpty())
			{
				const FMRItemDef* Item = Data ? Data->FindItemByIcon(FPaths::GetBaseFilename(Icon).ToLower()) : nullptr;
				FMRQuickSlot& Q = QuickSlots[i];
				Q.Id = Item ? Item->Class : FName(*FPaths::GetBaseFilename(Icon).ToLower());
				Q.Icon = Icon;
				Q.Name = Name;
			}
			FMRQuickSlot& Spell = QuickSlots[FMRQuickSlot::PerRow + i];
			if (S.SpellBar.IsValidIndex(i) && !S.SpellBar[i].IsEmpty() && Spell.IsEmpty())
			{
				Spell.bSpell = true;
				Spell.Id = FName(*S.SpellBar[i]);
			}
		}
		UE_LOG(LogMeridian, Log, TEXT("MRNet: the hotbar and spell bar kept before became quick slots"));
		S.Hotbar.Reset();
		S.SpellBar.Reset();
		SaveLayout();
	}
	Rebuild();
}

void UMRNetInventory::SaveLayout()
{
	UMRNetSubsystem* N = Net.Get();
	if (!N)
	{
		return;
	}
	SaveQuickSlots(N->GetSocial().QuickSlots);
	N->SaveSocial();
}

const FMRNetObject* UMRNetInventory::FindObject(const FMRSlotContent& Content) const
{
	const UMRNetSubsystem* N = Net.Get();
	return N && Content.ObjectId ? N->FindInventory(Content.ObjectId) : nullptr;
}

bool UMRNetInventory::IsInUse(const FMRSlotContent& Content) const
{
	const UMRNetSubsystem* N = Net.Get();
	return N && Content.ObjectId && N->IsUsing(Content.ObjectId);
}

FMRSlotContent UMRNetInventory::ContentOf(const FMRNetObject& O) const
{
	FMRSlotContent C;
	const FString Stem = FPaths::GetBaseFilename(O.Icon).ToLower();
	const FMRItemDef* Item = Data ? Data->FindItemByIcon(Stem) : nullptr;
	C.Id = Item ? Item->Class : FName(*Stem);
	C.Count = O.bNumber ? FMath::Max<int32>(1, O.Amount) : 1;
	C.ObjectId = O.Id;
	return C;
}

EMREquipSlot UMRNetInventory::SlotFor(const FMRSlotContent& C) const
{
	const FMRItemDef* Item = Data ? Data->FindItem(C.Id) : nullptr;
	return Item ? Data->EquipSlotFor(*Item) : EMREquipSlot::Count;
}

UMRInventorySource::FQuickItem UMRNetInventory::FindQuickItem(const FMRQuickSlot& Slot) const
{
	// by the server's name first (an identified item's real name), else by class: the one in use first
	// (using the slot again puts a wielded weapon away), else the first carried; Count: how many in all
	FQuickItem Found;
	const UMRNetSubsystem* N = Net.Get();
	if (!N || Slot.IsEmpty() || Slot.bSpell)
	{
		return Found;
	}
	const TArray<FMRNetObject>& Inv = N->GetInventory();
	bool bByName = false;
	for (const FMRNetObject& O : Inv)
	{
		if (!Slot.Name.IsEmpty() && O.Name.Equals(Slot.Name, ESearchCase::IgnoreCase))
		{
			bByName = true;
			break;
		}
	}
	int32 Count = 0;
	for (const FMRNetObject& O : Inv)
	{
		const FMRSlotContent C = ContentOf(O);
		if (bByName ? !O.Name.Equals(Slot.Name, ESearchCase::IgnoreCase) : C.Id != Slot.Id)
		{
			continue;
		}
		Count += C.Count;
		const bool bUsing = N->IsUsing(O.Id);
		if (Found.Content.IsEmpty() || (bUsing && !Found.bInUse))
		{
			Found.Content = C;
			Found.bInUse = bUsing;
		}
	}
	Found.Content.Count = Count;
	return Found;
}

FMRQuickSlot UMRNetInventory::QuickSlotFor(const FMRSlotContent& Content) const
{
	// a server item: its own name (an identified item's real name) and bitmap
	FMRQuickSlot Q = Super::QuickSlotFor(Content);
	if (const FMRNetObject* O = !Content.bSpell ? FindObject(Content) : nullptr)
	{
		Q.Name = O->Name;
		Q.Icon = O->Icon;
	}
	return Q;
}

void UMRNetInventory::Rebuild()
{
	const UMRNetSubsystem* N = Net.Get();
	BagView.Reset();
	for (FMRSlotContent& C : EquipView)
	{
		C = FMRSlotContent();
	}
	if (!N)
	{
		Changed();
		return;
	}
	const TArray<FMRNetObject>& Inv = N->GetInventory();
	TSet<uint32> Placed;
	// the carried item (picked up whole) isn't shown where it was
	if (!Cursor.IsEmpty() && Cursor.ObjectId)
	{
		const FMRNetObject* O = N->FindInventory(Cursor.ObjectId);
		if (!O)
		{
			Cursor = FMRSlotContent();  // it's gone (dropped, used up)
		}
		else if (!O->bNumber || Cursor.Count >= static_cast<int32>(O->Amount))
		{
			Placed.Add(Cursor.ObjectId);
		}
	}
	// in use: on its equipment slot (a second ring on Ring2, a second hand item in the free hand)
	for (const FMRNetObject& O : Inv)
	{
		if (!N->IsUsing(O.Id) || Placed.Contains(O.Id))
		{
			continue;
		}
		const FMRSlotContent C = ContentOf(O);
		EMREquipSlot E = SlotFor(C);
		if (E == EMREquipSlot::Ring1 && !EquipView[static_cast<int32>(E)].IsEmpty())
		{
			E = EMREquipSlot::Ring2;
		}
		if ((E == EMREquipSlot::RightHand || E == EMREquipSlot::LeftHand) && !EquipView[static_cast<int32>(E)].IsEmpty())
		{
			E = E == EMREquipSlot::RightHand ? EMREquipSlot::LeftHand : EMREquipSlot::RightHand;
		}
		if (E != EMREquipSlot::Count && EquipView[static_cast<int32>(E)].IsEmpty())
		{
			EquipView[static_cast<int32>(E)] = C;
			Placed.Add(O.Id);
		}
	}
	// the rest, in the server's order
	for (const FMRNetObject& O : Inv)
	{
		if (!Placed.Contains(O.Id))
		{
			BagView.Add(ContentOf(O));
		}
	}
	Changed();
}

void UMRNetInventory::ClearCursor()
{
	Cursor = FMRSlotContent();
	CursorOrigin = FMRSlotRef();
	Rebuild();
}

bool UMRNetInventory::Accepts(const FMRSlotRef& Slot, const FMRSlotContent& Content) const
{
	if (Slot.Area == EMRSlotArea::Equipment && !Content.IsEmpty() && !Content.bSpell)
	{
		// the item's own slot (a ring either ring slot); an item the client doesn't know: let the server say
		const FMRItemDef* Item = Data ? Data->FindItem(Content.Id) : nullptr;
		if (!Item)
		{
			return Content.ObjectId != 0;
		}
		const EMREquipSlot Fit = Data->EquipSlotFor(*Item);
		const EMREquipSlot S = static_cast<EMREquipSlot>(Slot.Index);
		return Fit == S || (Fit == EMREquipSlot::Ring1 && S == EMREquipSlot::Ring2)
			|| ((Fit == EMREquipSlot::RightHand || Fit == EMREquipSlot::LeftHand) && (S == EMREquipSlot::RightHand || S == EMREquipSlot::LeftHand));
	}
	return Super::Accepts(Slot, Content);
}

void UMRNetInventory::Click(const FMRSlotRef& Slot, bool bRight)
{
	UMRNetSubsystem* N = Net.Get();
	const FMRSlotContent In = GetRaw(Slot);
	// spells and quick slots: the base class's rules (they live on this client)
	if (Slot.Area == EMRSlotArea::QuickSlot || Slot.Area == EMRSlotArea::SpellBook || Cursor.bSpell || (Cursor.IsEmpty() && In.bSpell))
	{
		Super::Click(Slot, bRight);
		return;
	}
	if (!N)
	{
		return;
	}
	if (Cursor.IsEmpty())
	{
		if (In.IsEmpty() || !In.ObjectId)
		{
			return;
		}
		// pick it up (right click: half of a number item, for dropping some)
		Cursor = In;
		if (bRight && In.Count > 1)
		{
			Cursor.Count = (In.Count + 1) / 2;
		}
		CursorOrigin = Slot;
		Rebuild();
		return;
	}
	const uint32 Id = Cursor.ObjectId;
	const bool bFromEquip = CursorOrigin.Area == EMRSlotArea::Equipment;
	if (Slot == CursorOrigin)
	{
		ClearCursor();  // put back
		return;
	}
	switch (Slot.Area)
	{
	case EMRSlotArea::Equipment:
		if (Accepts(Slot, Cursor) && !bFromEquip)
		{
			N->UseItem(Id);
			ClearCursor();
		}
		return;
	case EMRSlotArea::Bag:
		if (bFromEquip)
		{
			N->UnuseItem(Id);
		}
		// onto another item in the bag: take its place in the server's list (not items in use)
		if (In.ObjectId && In.ObjectId != Id && !bFromEquip && !N->IsUsing(In.ObjectId) && !N->IsUsing(Id))
		{
			N->MoveInventoryItem(Id, In.ObjectId);
		}
		ClearCursor();
		return;
	default:
		return;
	}
}

void UMRNetInventory::QuickMove(const FMRSlotRef& Slot)
{
	UMRNetSubsystem* N = Net.Get();
	const FMRSlotContent In = GetRaw(Slot);
	if (Slot.Area == EMRSlotArea::QuickSlot || Slot.Area == EMRSlotArea::SpellBook)
	{
		Super::QuickMove(Slot);
		return;
	}
	if (!N || In.IsEmpty() || !In.ObjectId)
	{
		return;
	}
	if (Slot.Area == EMRSlotArea::Equipment || N->IsUsing(In.ObjectId))
	{
		N->UnuseItem(In.ObjectId);  // take it off
		return;
	}
	// use it: wear, wield, or whatever using does for it (eat, read): the server says
	N->UseItem(In.ObjectId);
}

void UMRNetInventory::UseQuickItem(int32 Index)
{
	// the server's use: wield, wear, eat, read...; the one in use is taken off (a weapon put away)
	UMRNetSubsystem* N = Net.Get();
	const FQuickItem Item = FindQuickItem(GetQuickSlot(Index));
	if (!N || !Item.Content.ObjectId)
	{
		return;
	}
	if (Item.bInUse)
	{
		N->UnuseItem(Item.Content.ObjectId);
	}
	else
	{
		N->UseItem(Item.Content.ObjectId);
	}
}

void UMRNetInventory::DropCursor(bool bOne)
{
	if (Cursor.bSpell || Cursor.IsEmpty())
	{
		Super::DropCursor(bOne);
		return;
	}
	if (UMRNetSubsystem* N = Net.Get())
	{
		const FMRNetObject* O = N->FindInventory(Cursor.ObjectId);
		// a number item: how many (right click: one); anything else goes whole
		N->Drop(Cursor.ObjectId, O && O->bNumber ? static_cast<uint32>(bOne ? 1 : Cursor.Count) : 0);
	}
	ClearCursor();
}

void UMRNetInventory::ReturnCursor()
{
	if (Cursor.bSpell)
	{
		Super::ReturnCursor();
		return;
	}
	ClearCursor();
}
