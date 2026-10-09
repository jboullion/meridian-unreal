#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRFireActor.generated.h"

class UMaterialBillboardComponent;
class UMaterialInterface;
class UPointLightComponent;

/**
 * A fire placed by the world build (tools/ue/build_world.py, docs/adr/0005 phase 3): wall torches
 * found in the blockout, braziers and candles from the Kod objects, and the original's lights in
 * flickering sectors. Two optional parts:
 *  - Flame: a camera-facing sprite with an M_Fire instance (a flipbook of the original's flame frames,
 *    each fire cycling from its own phase in the material), at the original's size.
 *  - Light: a point light that UMRFireSubsystem flickers (seeded noise, so neighbours never pulse
 *    together), switches off beyond a distance, and lets cast shadows only within the shadow budget.
 * Client-side decoration only: no collision, not replicated.
 */
UCLASS()
class UNREALMERIDIAN_API AMRFireActor : public AActor
{
	GENERATED_BODY()

public:
	AMRFireActor();

	/** Editor scripting: the flame sprite (full width and height in cm, centred on the actor). No material: no flame. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Fire")
	void SetFlame(UMaterialInterface* Material, float WidthCm, float HeightCm);

	/** Editor scripting: the light. Candela <= 0: no light. bShadowEligible: may cast shadows when it
	    is among the nearest fires (mr.Fire.ShadowBudget); otherwise it never does. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Fire")
	void SetLight(float Candela, float RadiusCm, FLinearColor Color, FVector OffsetCm, float SourceRadiusCm,
		bool bInFlicker, bool bInShadowEligible, int32 InSeed);

	UPointLightComponent* GetLight() const { return Light; }
	bool HasLight() const { return BaseIntensity > 0.f; }

	/** The light's settings as built: the subsystem modulates around them. */
	UPROPERTY(VisibleAnywhere, Category = "Fire")
	float BaseIntensity = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	FLinearColor BaseColor = FLinearColor::White;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	FVector BaseLightOffset = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	bool bFlicker = true;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	bool bShadowEligible = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UMaterialBillboardComponent> Flame;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;
};
