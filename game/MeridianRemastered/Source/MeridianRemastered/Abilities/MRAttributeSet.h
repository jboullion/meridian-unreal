#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MRAttributeSet.generated.h"

#define MR_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Character attributes, ported from Kod battler/player.kod.
 *
 * The six stats (Might, Intellect, Stamina, Agility, Mysticism, Aim) are the original 1..50
 * character-creation stats. Health, Mana and Vigor follow the original's rules:
 *   - Health starts at 20; MaxHealth rises with experience (TryGainBaseMaxHealth), hard cap 150.
 *     Kod stores current health x100 for precision; here it is a float, so no scaling is needed.
 *   - Mana starts at 20; base max = 15 + Mysticism / 5 plus nodes, items and enchantments.
 *   - Vigor runs 0..200 and starts at 100. In the remaster it is the Skyrim-style stamina bar:
 *     sprinting, power attacks, blocking, dodging and casting all draw on it.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMRAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;

	// --- the six original stats
	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Might)
	FGameplayAttributeData Might;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Might)

	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Intellect)
	FGameplayAttributeData Intellect;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Intellect)

	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Agility)
	FGameplayAttributeData Agility;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Agility)

	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Mysticism)
	FGameplayAttributeData Mysticism;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Mysticism)

	UPROPERTY(BlueprintReadOnly, Category = "Stats", ReplicatedUsing = OnRep_Aim)
	FGameplayAttributeData Aim;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Aim)

	// --- pools
	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Mana)

	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, MaxMana)

	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_Vigor)
	FGameplayAttributeData Vigor;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, Vigor)

	UPROPERTY(BlueprintReadOnly, Category = "Pools", ReplicatedUsing = OnRep_MaxVigor)
	FGameplayAttributeData MaxVigor;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, MaxVigor)

	// --- meta attribute: incoming damage, applied to Health in PostGameplayEffectExecute (server only)
	UPROPERTY(BlueprintReadOnly, Category = "Meta")
	FGameplayAttributeData IncomingDamage;
	MR_ATTRIBUTE_ACCESSORS(UMRAttributeSet, IncomingDamage)

	/** Original hard cap on max hit points (blakston.khd MAX_HP). */
	static constexpr float MaxHitPointsCap = 150.f;
	static constexpr float MaxVigorDefault = 200.f;

protected:
	UFUNCTION() void OnRep_Might(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Intellect(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Stamina(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Agility(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Mysticism(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Aim(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxMana(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Vigor(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxVigor(const FGameplayAttributeData& Old);
};
