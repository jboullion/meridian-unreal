#include "Player/MRPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Net/UnrealNetwork.h"

AMRPlayerState::AMRPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed: gameplay effects replicate to the owning client only; cues and tags go to everyone.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UMRAttributeSet>(TEXT("AttributeSet"));

	// Attributes change often in combat; the default player state update rate is too low.
	SetNetUpdateFrequency(30.f);
}

UAbilitySystemComponent* AMRPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMRPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMRPlayerState, ZoneId);
}

void AMRPlayerState::SetZoneId(int32 NewZoneId)
{
	if (NewZoneId == ZoneId)
	{
		return;
	}
	const int32 Old = ZoneId;
	ZoneId = NewZoneId;
	OnZoneChanged.Broadcast(Old, NewZoneId);
}

void AMRPlayerState::OnRep_ZoneId(int32 OldZoneId)
{
	OnZoneChanged.Broadcast(OldZoneId, ZoneId);
}
