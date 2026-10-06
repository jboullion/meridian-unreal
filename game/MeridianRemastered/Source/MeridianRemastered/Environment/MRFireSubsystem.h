#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRFireSubsystem.generated.h"

class AMRFireActor;

/**
 * Drives every fire's light (docs/adr/0005 phase 3) on clients, never the dedicated server:
 *  - Flicker: the intensity follows seeded noise, about the original's +-40 of 255
 *    (mr.Fire.Amplitude), a little warmer as it dips, and the source wanders a centimetre or two
 *    (mr.Fire.JitterCm). Each fire has its own seed, so neighbouring torches never pulse together.
 *    Beyond mr.Fire.FlickerDistanceM the light holds still; beyond mr.Fire.CullDistanceM it's off.
 *  - Shadows: fire lights are unshadowed; the mr.Fire.ShadowBudget nearest that are eligible (props.json
 *    "shadows") cast shadows, re-chosen four times a second.
 * mr.Fire.Flicker 0 holds every light steady (comparisons).
 */
UCLASS()
class MERIDIANREMASTERED_API UMRFireSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The flicker for a fire at a time: -1..1, deterministic in (Seconds, Seed). Pure, for tests. */
	static float FlickerAt(double Seconds, int32 Seed);

	void Register(AMRFireActor* Fire);
	void Unregister(AMRFireActor* Fire);
	int32 NumFires() const { return Fires.Num(); }

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool ViewLocation(FVector& Out) const;

	TArray<TWeakObjectPtr<AMRFireActor>> Fires;
	TSet<TWeakObjectPtr<AMRFireActor>> Shadowed;
	double NextShadowPick = 0.0;
	int32 LoggedFires = -1;
};
