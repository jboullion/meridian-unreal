#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "MRPlayerState.generated.h"

class UAbilitySystemComponent;
class UMRAttributeSet;

/**
 * Owns the player's Ability System Component and attributes, so stats, skills and spells
 * survive the pawn being destroyed on death (ragdoll) and respawned.
 */
UCLASS()
class UNREALMERIDIAN_API AMRPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMRPlayerState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMRAttributeSet* GetAttributeSet() const { return AttributeSet; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Original room ID (RID) of the zone this player is in; drives zone flags, music and relevancy. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	int32 GetZoneId() const { return ZoneId; }
	void SetZoneId(int32 NewZoneId);

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnZoneChanged, int32 /*Old*/, int32 /*New*/);
	FOnZoneChanged OnZoneChanged;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UMRAttributeSet> AttributeSet;

	UPROPERTY(ReplicatedUsing = OnRep_ZoneId)
	int32 ZoneId = 0;

	UFUNCTION()
	void OnRep_ZoneId(int32 OldZoneId);
};
