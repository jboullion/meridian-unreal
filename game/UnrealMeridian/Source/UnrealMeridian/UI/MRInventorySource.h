#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/MRUITypes.h"
#include "MRInventorySource.generated.h"

class UMRGameDataSubsystem;

/**
 * What the UI shows and changes: the bag, the equipment, the quick slots, known spells and skills,
 * and the stack carried by the mouse (docs/adr/0009-user-interface.md).
 *
 * This is the seam between the widgets and the game. The interactions (Minecraft's click rules,
 * below) are implemented here on top of Get/Set, so UMRMockInventory only stores arrays. The
 * server-owned inventory will override the interactions to send them to the server instead, and
 * the widgets won't change.
 *
 * Minecraft's rules, with the cursor (the carried stack):
 * - left click: pick a stack up; put it down; merge onto the same item; swap with a different one
 * - right click: pick half up; put one down
 * - shift click: equip from the bag (else name it on a free quick slot); unequip to the bag
 * - clicking outside the window drops the carried stack (right click: one)
 * Spells: the spell book is read only (picking a spell up copies it); one dropped anywhere but a
 * quick slot is let go.
 *
 * Quick slots (Meridian Shards' quick slots; since 2026-10-14 in place of the item hotbar and the
 * spell bar) name a spell or an item, they don't hold it: the item stays in the bag. Dropping a
 * carried stack or spell on one names it there and the stack goes back where it came from; a
 * number key over a slot in the dialog does the same for that quick slot.
 */
UCLASS(Abstract)
class UNREALMERIDIAN_API UMRInventorySource : public UObject
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
	/** What a slot holds (an item, a spell) named on quick slot Index (a number key over it in the dialog). */
	void AssignQuickSlot(const FMRSlotRef& Slot, int32 Index);
	virtual void DropCursor(bool bOne);
	/** Put the carried stack back (closing the window): into its old slot or any free one. */
	virtual void ReturnCursor();
	/**
	 * The carried item dropped on the avatar: put it on, in its own equipment slot (a free one
	 * first: the second ring slot for a second ring). False if it has none.
	 */
	bool EquipCarried();

	// --- quick slots
	const FMRQuickSlot& GetQuickSlot(int32 Index) const;
	void SetQuickSlot(int32 Index, const FMRQuickSlot& Slot);
	void SwapQuickSlots(int32 A, int32 B);
	/** The quick slot naming this content (an item or a spell), from what a slot holds. */
	virtual FMRQuickSlot QuickSlotFor(const FMRSlotContent& Content) const;
	/** What a quick slot's item stands for now: the one in use first, else the first carried. */
	struct FQuickItem
	{
		/** That item (empty: none carried); its Count is how many are carried in all. */
		FMRSlotContent Content;
		bool bInUse = false;
	};
	virtual FQuickItem FindQuickItem(const FMRQuickSlot& Slot) const;
	/** Whether a quick slot's spell is known, or its item carried. */
	bool IsQuickSlotAvailable(int32 Index) const;
	bool IsQuickSlotInUse(int32 Index) const;
	/**
	 * Use a quick slot's item: put it on (in its own slot; a second ring on the second ring slot), or
	 * take it off when it's on already (Shards: using the slot again puts a wielded weapon away).
	 * Online, whatever using does (eat, read): the server says. Spells are cast by the UI.
	 */
	virtual void UseQuickItem(int32 Index);

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
	/** The quick slots changed: online they're kept with the character. */
	virtual void OnQuickSlotsChanged() {}
	/** Fill the quick slots from a saved list (FMRQuickSlot::ToString each). */
	void LoadQuickSlots(const TArray<FString>& Saved);
	void SaveQuickSlots(TArray<FString>& Out) const;

	FMRQuickSlot QuickSlots[FMRQuickSlot::Num];
	/** Where the carried stack was picked up (ReturnCursor). */
	FMRSlotRef CursorOrigin;
};

/**
 * The UI's mock inventory: local, not replicated, seeded from data/ui/mock_inventory.json. Until
 * the server owns inventory (docs/adr/0009-user-interface.md, "the seam").
 */
UCLASS()
class UNREALMERIDIAN_API UMRMockInventory : public UMRInventorySource
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
	FMRSlotContent Equipment[static_cast<int32>(EMREquipSlot::Count)];
	FMRSlotContent Cursor;
	TArray<FName> KnownSpells;
	TMap<FName, int32> SpellPercents;
	TMap<FName, int32> Skills;
	TArray<FMRStatView> Stats;
};

/**
 * The UI's source while playing on a server (docs/adr/0012-client-parity-and-world-coverage.md M3):
 * what the character carries (BP_INVENTORY), what it has in use (BP_USE_LIST), its spells and skills
 * as the server lists them (stat groups 3 and 4, by name into our data), and the quick slots, kept
 * on this client with the character.
 *
 * Where an item shows: in use, on its equipment slot (by its class's use type: the wielded weapon in
 * the right hand); else in the bag, in the server's order.
 *
 * The click rules are Minecraft's, made into requests (the server decides; the UI shows what it
 * then says): onto an equipment slot uses the item (BP_REQ_USE), off one takes it off
 * (BP_REQ_UNUSE), onto a bag slot moves it there (BP_REQ_INVENTORY_MOVE), out of the window drops
 * it (BP_REQ_DROP; right click: one of a number item), shift click uses or takes off.
 */
UCLASS()
class UNREALMERIDIAN_API UMRNetInventory : public UMRInventorySource
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
	virtual FMRQuickSlot QuickSlotFor(const FMRSlotContent& Content) const override;
	virtual FQuickItem FindQuickItem(const FMRQuickSlot& Slot) const override;
	virtual void UseQuickItem(int32 Index) override;
	virtual void DropCursor(bool bOne) override;
	virtual void ReturnCursor() override;

protected:
	virtual FMRSlotContent GetRaw(const FMRSlotRef& Slot) const override;
	virtual void SetRaw(const FMRSlotRef& Slot, const FMRSlotContent& Content) override;
	virtual FMRSlotRef Resolve(const FMRSlotRef& Slot) const override { return Slot; }
	virtual void OnQuickSlotsChanged() override { SaveLayout(); }

private:
	/** Lay the server's inventory out into the bag and equipment views. */
	void Rebuild();
	FMRSlotContent ContentOf(const struct FMRNetObject& O) const;
	/** The equipment slot an item in use shows on (Count: none known). */
	EMREquipSlot SlotFor(const FMRSlotContent& C) const;
	/** The quick slots kept per character (UMRNetSubsystem::GetSocial; before 2026-10-14 its hotbar and spell bar). */
	void LoadLayout();
	void SaveLayout();
	void ClearCursor();

	TWeakObjectPtr<class UMRNetSubsystem> Net;
	FDelegateHandle InventoryHandle;
	FDelegateHandle LayoutHandle;
	TArray<FName> KnownSpells;
	TMap<FName, int32> SpellPercents;
	TMap<FName, int32> Skills;
	FMRSlotContent Cursor;
	TArray<FMRSlotContent> BagView;
	FMRSlotContent EquipView[static_cast<int32>(EMREquipSlot::Count)];
};
