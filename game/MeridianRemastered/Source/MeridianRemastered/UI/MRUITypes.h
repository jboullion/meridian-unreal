#pragma once

#include "CoreMinimal.h"

/**
 * Shared types of the in-game UI (docs/adr/0009-user-interface.md). Plain C++: only the Slate
 * widgets and the inventory source use them.
 */

/** Where a slot lives. */
enum class EMRSlotArea : uint8
{
	None,
	Bag,        // the scrollable inventory (grows: the original limits by weight and bulk, not slots)
	Hotbar,     // the 9 item slots at the bottom (Minecraft-literal: real storage)
	Equipment,  // worn and wielded (EMREquipSlot)
	SpellBar,   // the 9 numpad spell shortcuts
	SpellBook,  // known spells (read only: picking one up copies it)
	Cursor,     // the stack carried by the mouse
};

/** Equipment slots, around the avatar. The right hand is the selected hotbar slot (Minecraft's main hand). */
enum class EMREquipSlot : uint8
{
	Head,
	Amulet,
	Torso,
	Hands,
	Legs,
	Ring1,
	Ring2,
	LeftHand,
	RightHand,
	Count
};

/** Kod item use types (blakston.khd ITEM_USE_*): which equipment slot an item fits. */
namespace MRItemUse
{
	constexpr int32 Hand = 0x0008;
	constexpr int32 Body = 0x0010;
	constexpr int32 Neck = 0x0040;
	constexpr int32 Finger = 0x0080;
	constexpr int32 Gauntlet = 0x0100;
	constexpr int32 Head = 0x0200;
	constexpr int32 Legs = 0x0400;
	constexpr int32 Shirt = 0x0800;
	constexpr int32 Face = 0x1000;
}

struct FMRSlotRef
{
	EMRSlotArea Area = EMRSlotArea::None;
	int32 Index = 0;

	FMRSlotRef() = default;
	FMRSlotRef(EMRSlotArea InArea, int32 InIndex) : Area(InArea), Index(InIndex) {}
	static FMRSlotRef Equip(EMREquipSlot Slot) { return FMRSlotRef(EMRSlotArea::Equipment, static_cast<int32>(Slot)); }

	bool IsValid() const { return Area != EMRSlotArea::None; }
	bool operator==(const FMRSlotRef& O) const { return Area == O.Area && Index == O.Index; }
	bool operator!=(const FMRSlotRef& O) const { return !(*this == O); }
};

/**
 * What a slot holds: an item stack, or a spell (spell bar, spell book, cursor). Online an item is a
 * server object (ObjectId); Id is then its class in data/items.json when its icon is known there
 * (UMRGameDataSubsystem::FindItemByIcon), else its icon.
 */
struct FMRSlotContent
{
	FName Id;
	int32 Count = 0;
	bool bSpell = false;
	uint32 ObjectId = 0;

	FMRSlotContent() = default;
	FMRSlotContent(FName InId, int32 InCount, bool bInSpell = false) : Id(InId), Count(InCount), bSpell(bInSpell) {}
	static FMRSlotContent Spell(FName InId) { return FMRSlotContent(InId, 1, true); }

	bool IsEmpty() const { return Id.IsNone() || Count <= 0; }
	bool SameThing(const FMRSlotContent& O) const { return ObjectId || O.ObjectId ? ObjectId == O.ObjectId : Id == O.Id && bSpell == O.bSpell; }
};

/** An item class from data/items.json (tools/kod_extract). */
struct FMRItemDef
{
	FName Class;
	FText Name;
	FText Desc;
	FName Icon;  // bgf stem: /Game/Generated/UI/Icons/T_Icon_<icon>
	FString Kind;  // Weapon, DefenseModifier, Item
	int32 Weight = 0;
	int32 Bulk = 0;
	int32 Value = 0;
	int32 UseType = 0;
	int32 DamageMin = 0;
	int32 DamageMax = 0;
	int32 Defense = 0;
	bool bStackable = false;  // a Kod NumberItem
};

struct FMRReagent
{
	FName Item;
	int32 Count = 0;
};

/** A spell from data/spells.json. */
struct FMRSpellDef
{
	FName Class;
	FText Name;
	FText Desc;
	FName Icon;
	int32 School = 0;  // SS_*: 1 Shal'ille, 2 Qor, 3 Kraanan, 4 Faren, 5 Riija, 6 Jala
	int32 Level = 0;
	int32 Mana = 0;
	TArray<FMRReagent> Reagents;
};

/** What an NPC does with money and goods (data/net/npcs.json, from Kod's MOB_* attributes). */
namespace MRNpcRole
{
	constexpr uint8 Buyer = 0x01;     // buys: Sell
	constexpr uint8 Seller = 0x02;    // sells: Buy
	constexpr uint8 Banker = 0x04;    // keeps shillings: Bank
	constexpr uint8 Vaultman = 0x08;  // keeps items: Withdraw, Deposit
}

/** A line of the Quests page (the server's stat group 5): a heading (no object), or a quest. */
struct FMRQuestView
{
	FString Name;
	/** The quest's object (looked at for its description); 0 for a heading. */
	uint32 ObjectId = 0;
	/** Its bitmap ("quest.bgf"), for the icon. */
	FString Icon;
};

/** One stat line on the Stats page: the server's (online) or the mock list's (offline). */
struct FMRStatView
{
	FString Name;
	int32 Value = 0;
	/** What the bar fills to (the server's current maximum); 0 = no bar. */
	int32 Max = 0;
	int32 Min = 0;
	/** A stat whose value is text (a resource), shown instead of the number. */
	FString Text;
};

/** A section of the Stats page (data/ui/stat_layout.json). */
struct FMRStatSection
{
	FText Title;
	/** Spendable points: drawn apart, at the top. */
	bool bPoints = false;
	TArray<FMRStatView> Stats;
};

/** A skill from data/skills.json. */
struct FMRSkillDef
{
	FName Class;
	FText Name;
	FText Desc;
	FName Icon;
	int32 School = 0;
	int32 Level = 0;
};
