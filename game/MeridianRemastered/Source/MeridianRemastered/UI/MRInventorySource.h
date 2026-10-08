#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/MRUITypes.h"
#include "MRInventorySource.generated.h"

class UMRGameDataSubsystem;

/**
 * What the UI shows and changes: the bag, the 9 hotbar slots, the equipment, the spell bar, known
 * spells and skills, and the stack carried by the mouse (docs/adr/0009-user-interface.md).
 *
 * This is the seam between the widgets and the game. The interactions (Minecraft's click rules,
 * below) are implemented here on top of Get/Set, so UMRMockInventory only stores arrays. The
 * server-owned inventory will override the interactions to send them to the server instead, and
 * the widgets won't change.
 *
 * Minecraft's rules, with the cursor (the carried stack):
 * - left click: pick a stack up; put it down; merge onto the same item; swap with a different one
 * - right click: pick half up; put one down
 * - shift click: move between the bag and the hotbar; equip from the bag; unequip to the bag
 * - a number key over a slot: swap it with that hotbar slot
 * - clicking outside the window drops the carried stack (right click: one)
 * Spells: the spell book is read only (picking a spell up copies it); spells only go on the spell
 * bar, and one dropped anywhere else is let go.
 */
UCLASS(Abstract)
class MERIDIANREMASTERED_API UMRInventorySource : public UObject
{
	GENERATED_BODY()

public:
	/** Number of slots to show in an area (the bag always has a free row). */
	virtual int32 NumSlots(EMRSlotArea Area) const;
	/** The content of a slot. The right hand is the selected hotbar slot. */
	FMRSlotContent Get(const FMRSlotRef& Slot) const;
	/** Whether this content may go in this slot. */
	virtual bool Accepts(const FMRSlotRef& Slot, const FMRSlotContent& Content) const;
	/** Most of one thing a slot holds. */
	int32 MaxStack(const FMRSlotRef& Slot, const FMRSlotContent& Content) const;

	// --- interactions (virtual: a server-owned inventory sends them instead)
	virtual void Click(const FMRSlotRef& Slot, bool bRight);
	virtual void QuickMove(const FMRSlotRef& Slot);
	virtual void SwapWithHotbar(const FMRSlotRef& Slot, int32 HotbarIndex);
	virtual void DropCursor(bool bOne);
	/** Put the carried stack back (closing the window): into its old slot or any free one. */
	virtual void ReturnCursor();
	virtual void SelectHotbar(int32 Index);
	int32 GetSelectedHotbar() const { return SelectedHotbar; }

	/** Known spells (spell book order) and skills with their percentages. */
	virtual const TArray<FName>& GetKnownSpells() const PURE_VIRTUAL(UMRInventorySource::GetKnownSpells, static TArray<FName> None; return None;);
	virtual const TMap<FName, int32>& GetSkills() const PURE_VIRTUAL(UMRInventorySource::GetSkills, static TMap<FName, int32> None; return None;);
	/** A known spell's ability percentage, or -1 if unknown (the server sends it with the spell list). */
	virtual int32 GetSpellPercent(FName Spell) const { return -1; }
	/** The character's stats in the server's order (the Stats page; online, UMRUISubsystem uses the server's own list). */
	virtual const TArray<FMRStatView>& GetStats() const { static TArray<FMRStatView> None; return None; }

	/** Totals of everything carried (bag, hotbar, equipment): the original's weight and bulk. */
	void GetTotals(int32& OutWeight, int32& OutBulk) const;

	/** Broadcast after anything changed. */
	FSimpleMulticastDelegate OnChanged;
	/** Broadcast when the selected hotbar slot changes (the HUD shows the item's name). */
	FSimpleMulticastDelegate OnSelectionChanged;

	void SetData(UMRGameDataSubsystem* InData) { Data = InData; }
	UMRGameDataSubsystem* GetData() const { return Data; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMRGameDataSubsystem> Data;

	/** Storage. Slot refs given here are resolved (never the right hand). */
	virtual FMRSlotContent GetRaw(const FMRSlotRef& Slot) const PURE_VIRTUAL(UMRInventorySource::GetRaw, return FMRSlotContent(););
	virtual void SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content) PURE_VIRTUAL(UMRInventorySource::SetRaw, );
	/** Called when the carried stack is dropped in the world. */
	virtual void OnDropped(const FMRSlotContent& Content);

	/** The right hand -> the selected hotbar slot (offline; online the server's wielded weapon has its own slot). */
	virtual FMRSlotRef Resolve(const FMRSlotRef& Slot) const;
	/** Put content into the first slots of an area that take it (merging first); returns what didn't fit. */
	FMRSlotContent Insert(EMRSlotArea Area, FMRSlotContent Content);
	void Changed() { OnChanged.Broadcast(); }

	int32 SelectedHotbar = 0;
	/** Where the carried stack was picked up (ReturnCursor). */
	FMRSlotRef CursorOrigin;
};

/**
 * The UI's mock inventory: local, not replicated, seeded from data/ui/mock_inventory.json. Until
 * the server owns inventory (docs/adr/0009-user-interface.md, "the seam").
 */
UCLASS()
class MERIDIANREMASTERED_API UMRMockInventory : public UMRInventorySource
{
	GENERATED_BODY()

public:
	void LoadFromJson();

	virtual const TArray<FName>& GetKnownSpells() const override { return KnownSpells; }
	virtual const TMap<FName, int32>& GetSkills() const override { return Skills; }
	virtual int32 GetSpellPercent(FName Spell) const override { const int32* P = SpellPercents.Find(Spell); return P ? *P : -1; }
	virtual const TArray<FMRStatView>& GetStats() const override { return Stats; }

protected:
	virtual FMRSlotContent GetRaw(const FMRSlotRef& Slot) const override;
	virtual void SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content) override;
	virtual void OnDropped(const FMRSlotContent& Content) override;

private:
	TArray<FMRSlotContent> Bag;
	FMRSlotContent Hotbar[9];
	FMRSlotContent Equipment[static_cast<int32>(EMREquipSlot::Count)];
	FMRSlotContent SpellBar[9];
	FMRSlotContent Cursor;
	TArray<FName> KnownSpells;
	TMap<FName, int32> SpellPercents;
	TMap<FName, int32> Skills;
	TArray<FMRStatView> Stats;
};

/**
 * The UI's source while playing on a server (docs/adr/0012-client-parity-and-world-coverage.md M3):
 * what the character carries (BP_INVENTORY), what it has in use (BP_USE_LIST), its spells and skills
 * as the server lists them (stat groups 3 and 4, by name into our data), and a spell bar and hotbar
 * layout kept on this client.
 *
 * Where an item shows: in use, on its equipment slot (by its class's use type: the wielded weapon in
 * the right hand, which is a slot of its own online); else on the hotbar slot it was put on (the
 * hotbar is a layout on this client); else in the bag, in the server's order.
 *
 * The click rules are Minecraft's, made into requests (the server decides; the UI shows what it
 * then says): onto an equipment slot uses the item (BP_REQ_USE), off one takes it off
 * (BP_REQ_UNUSE), onto a bag slot moves it there (BP_REQ_INVENTORY_MOVE), out of the window drops
 * it (BP_REQ_DROP; right click: one of a number item), shift click uses or takes off.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRNetInventory : public UMRInventorySource
{
	GENERATED_BODY()

public:
	virtual const TArray<FName>& GetKnownSpells() const override { return KnownSpells; }
	virtual const TMap<FName, int32>& GetSkills() const override { return Skills; }
	virtual int32 GetSpellPercent(FName Spell) const override { const int32* P = SpellPercents.Find(Spell); return P ? *P : -1; }

	void SetKnownSpells(const TArray<FName>& InSpells, const TMap<FName, int32>& InPercents);
	void SetSkills(const TMap<FName, int32>& InSkills);
	/** Follow this session's inventory (UMRNetSubsystem::OnInventoryChanged). */
	void SetNet(class UMRNetSubsystem* InNet);
	/** The server object behind a slot's content, or null. */
	const struct FMRNetObject* FindObject(const FMRSlotContent& Content) const;
	bool IsInUse(const FMRSlotContent& Content) const;

	virtual bool Accepts(const FMRSlotRef& Slot, const FMRSlotContent& Content) const override;
	virtual void Click(const FMRSlotRef& Slot, bool bRight) override;
	virtual void QuickMove(const FMRSlotRef& Slot) override;
	virtual void SwapWithHotbar(const FMRSlotRef& Slot, int32 HotbarIndex) override;
	virtual void DropCursor(bool bOne) override;
	virtual void ReturnCursor() override;

protected:
	virtual FMRSlotContent GetRaw(const FMRSlotRef& Slot) const override;
	virtual void SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content) override;
	virtual FMRSlotRef Resolve(const FMRSlotRef& Slot) const override { return Slot; }

private:
	/** Lay the server's inventory out into the bag, hotbar and equipment views. */
	void Rebuild();
	FMRSlotContent ContentOf(const struct FMRNetObject& O) const;
	/** The equipment slot an item in use shows on (Count: none known). */
	EMREquipSlot SlotFor(const FMRSlotContent& C) const;
	void PlaceOnHotbar(uint32 ObjectId, int32 Index);
	void RemoveFromHotbar(uint32 ObjectId);
	void ClearCursor();

	TWeakObjectPtr<class UMRNetSubsystem> Net;
	FDelegateHandle InventoryHandle;
	TArray<FName> KnownSpells;
	TMap<FName, int32> SpellPercents;
	TMap<FName, int32> Skills;
	FMRSlotContent SpellBar[9];
	FMRSlotContent Cursor;
	/** The hotbar layout: an object id per slot, and its icon and name to find it again after a save renumbers objects. */
	struct FHotbarRef
	{
		uint32 Id = 0;
		FString Key;
	};
	FHotbarRef HotbarRefs[9];
	TArray<FMRSlotContent> BagView;
	FMRSlotContent HotbarView[9];
	FMRSlotContent EquipView[static_cast<int32>(EMREquipSlot::Count)];
};
