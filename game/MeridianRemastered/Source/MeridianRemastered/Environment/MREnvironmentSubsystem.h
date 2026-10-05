#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Subsystems/WorldSubsystem.h"
#include "MREnvironmentSubsystem.generated.h"

class AActor;
class ULightComponent;
class UMaterialParameterCollection;

/**
 * The environment director (docs/adr/0005): on clients (never the dedicated server) it sets the
 * lighting of L_World - sun / moon, sky light, fog, post process - and MPC_Environment from
 * data/environment/moods.json, the game hour (UMRGameTimeSubsystem) and the local player's zone.
 *
 *  - A zone's cycle (moods.json "zones" -> "cycles") lists [hour, mood] keys. The two keys around
 *    the hour are blended: numbers and colours interpolate, everything else switches at the midpoint.
 *    Moods are applied as tools/ue/zone_mood.py applies them: blocks by actor (tag or editor label),
 *    properties on the actor's root component, "settings" on the post-process volume, "Collection"
 *    scalars on MPC_Environment.
 *  - The directional light follows the sun's path by day and the moon's by night (moods.json "sky"),
 *    which overrides the moods' Sun rotation, as do the moon's intensity and colour.
 *  - Lights tagged NightLamp are off from 11 to 17 as the original lamps are (kod lamp.kod), and
 *    MPC_Environment.LampsOn dims their glass.
 *  - Everything is re-evaluated every mr.Env.UpdateSeconds (5 s; a 2-hour game day moves the sun
 *    about 0.2 degrees in that time) and applied only when it changed, so shadow caches stay valid
 *    and a pinned hour (look-dev) applies once.
 *
 * -MRMood=<name> / mr.Env.Mood pins one mood (no cycle, the mood's own sun rotation): look-dev.
 * In editor worlds it only runs with mr.Env.Editor 1, so it never edits a level being worked on.
 * Console: MREnvReload re-reads moods.json.
 */
UCLASS()
class MERIDIANREMASTERED_API UMREnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Sun / moon light direction for a game hour: (pitch, yaw) of a directional light. Elevation in
	    degrees > 0 when above the horizon. Pure, for tests. */
	static FRotator SkyBodyRotation(double Hour, double Rise, double Set, double MaxElevation, double& OutElevation);

	/** The original lamps' switch: 0 from 11 to 17, 1 otherwise, with short fades (kod lamp.kod). */
	static float LampsOnForHour(double Hour);

	/** Re-read moods.json and apply on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Environment")
	void Reload();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TSharedPtr<FJsonObject> ResolveMood(const FString& Name);
	TSharedPtr<FJsonObject> StateFor(double Hour, int32 ZoneId, bool& bOutPinnedMood);
	void Apply(const TSharedPtr<FJsonObject>& State, double Hour, bool bPinnedMood);
	AActor* FindActor(const FString& Label) const;
	void ApplyLamps(float LampsOn);
	int32 LocalZoneId() const;

	TSharedPtr<FJsonObject> Root;
	TMap<FString, TSharedPtr<FJsonObject>> Resolved;
	TMap<TWeakObjectPtr<ULightComponent>, float> LampBase;
	TSet<FString> Warned;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	FString PinnedMood;
	FString LastApplied;
	double NextUpdate = 0.0;
	int32 LastZone = -1;
	bool bForce = true;
};
