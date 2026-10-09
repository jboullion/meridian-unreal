#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/MRUITypes.h"
#include "MRGameData.generated.h"

/**
 * The original's items, spells and skills (data/items.json, spells.json, skills.json from
 * tools/kod_extract), loaded once for the UI. Read on every machine that draws; the server will
 * use the same data when it owns inventory.
 */
UCLASS()
class UNREALMERIDIAN_API UMRGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const FMRItemDef* FindItem(FName Class) const { return Items.Find(Class); }
	/** An item class by its icon (the bgf's stem, "scimitar"): a server object's class, as near as the client can tell. */
	const FMRItemDef* FindItemByIcon(const FString& IconStem) const;
	const FMRSpellDef* FindSpell(FName Class) const { return Spells.Find(Class); }
	const FMRSkillDef* FindSkill(FName Class) const { return Skills.Find(Class); }
	/** By the name the server uses (vrName, any case): a server's spell and skill lists name them. */
	const FMRSpellDef* FindSpellByName(const FString& Name) const;
	const FMRSkillDef* FindSkillByName(const FString& Name) const;
	/** An NPC's trade roles by its name (MRNpcRole bits; 0: none known). */
	uint8 NpcRoles(const FString& Name) const;

	/** The equipment slot an item fits (Count if none). Rings fit Ring1 (and Ring2). */
	EMREquipSlot EquipSlotFor(const FMRItemDef& Item) const;

	/** Whether an item may go in this equipment slot. The right hand takes anything (it is the selected hotbar slot). */
	bool FitsEquipSlot(const FMRItemDef& Item, EMREquipSlot Slot) const;

	/** School name for SS_* (data/constants.json SS). */
	static FText SchoolName(int32 School);

private:
	TMap<FName, FMRItemDef> Items;
	TMap<FName, FMRSpellDef> Spells;
	TMap<FName, FMRSkillDef> Skills;
	TMap<FString, FName> SpellByName;
	TMap<FString, FName> ItemByIcon;
	TMap<FString, FName> SkillByName;
	TMap<FString, uint8> NpcRolesByName;
};
