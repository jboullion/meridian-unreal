#include "Abilities/MRAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UMRAttributeSet::UMRAttributeSet()
{
	// player.kod defaults for a fresh character; stats are placeholders until character creation
	InitMight(25.f);
	InitIntellect(25.f);
	InitStamina(25.f);
	InitAgility(25.f);
	InitMysticism(25.f);
	InitAim(25.f);
	InitMaxHealth(20.f);
	InitHealth(20.f);
	InitMaxMana(20.f);
	InitMana(20.f);
	InitMaxVigor(MaxVigorDefault);
	InitVigor(100.f);
	InitIncomingDamage(0.f);
}

void UMRAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Might, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Intellect, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Agility, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Mysticism, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Aim, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, Vigor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMRAttributeSet, MaxVigor, COND_None, REPNOTIFY_Always);
}

void UMRAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, MaxHitPointsCap);
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
	else if (Attribute == GetVigorAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxVigor());
	}
}

void UMRAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float Damage = GetIncomingDamage();
		SetIncomingDamage(0.f);
		if (Damage > 0.f)
		{
			SetHealth(FMath::Clamp(GetHealth() - Damage, 0.f, GetMaxHealth()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetVigorAttribute())
	{
		SetVigor(FMath::Clamp(GetVigor(), 0.f, GetMaxVigor()));
	}
}

void UMRAttributeSet::OnRep_Might(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Might, Old); }
void UMRAttributeSet::OnRep_Intellect(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Intellect, Old); }
void UMRAttributeSet::OnRep_Stamina(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Stamina, Old); }
void UMRAttributeSet::OnRep_Agility(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Agility, Old); }
void UMRAttributeSet::OnRep_Mysticism(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Mysticism, Old); }
void UMRAttributeSet::OnRep_Aim(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Aim, Old); }
void UMRAttributeSet::OnRep_Health(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Health, Old); }
void UMRAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, MaxHealth, Old); }
void UMRAttributeSet::OnRep_Mana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Mana, Old); }
void UMRAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, MaxMana, Old); }
void UMRAttributeSet::OnRep_Vigor(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, Vigor, Old); }
void UMRAttributeSet::OnRep_MaxVigor(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMRAttributeSet, MaxVigor, Old); }
