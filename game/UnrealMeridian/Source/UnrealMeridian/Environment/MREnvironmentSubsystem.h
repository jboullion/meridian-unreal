#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Environment/MRWeather.h"
#include "Subsystems/WorldSubsystem.h"
#include "MREnvironmentSubsystem.generated.h"

class AActor;
class AMRPrecipitationActor;
class ULightComponent;
class UMaterialInterface;
class UMaterialParameterCollection;

/** What the atmosphere shows (docs/adr/0005 phase 5): MPC_Environment's Night, Smoke, Motes, Pollen,
    Fireflies, Leaves, Spring, Autumn and Winter, and the snow that lies outdoors in winter. */
struct FMRAtmosphere
{
	float Night = 0.f;
	float Smoke = 0.f;
	float Motes = 0.f;
	float Pollen = 0.f;
	float Fireflies = 0.f;
	float Leaves = 0.f;
	float Spring = 0.f;
	float Autumn = 0.f;
	float Winter = 0.f;
	float WinterCover = 0.f;
};

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
 *  - Weather (phase 4): the zone profile names its weather zone and mask; AMRGameState says whether
 *    that zone storms and since when; the season and mask say rain or snow. A storm builds over 90 s
 *    and clears over 2 minutes: the zone's state blends toward the storm overlay mood (moods.json
 *    "weather"), outdoors the ground gets wet or white (MPC_Environment.Wetness, SnowCover), the wind
 *    rises (Wind), rain or snow falls around the camera (AMRPrecipitationActor; Precip, Snow), and
 *    rainstorms bring lightning (the "Lightning" light outdoors, the windows inside).
 *    mr.Weather.Particles 0 hides the falling rain and snow (a player setting, as in the original).
 *    Rain splashes on the ground and roofs, distant bolts flash in the sky with the lightning, and
 *    the original's rain, wind and thunder sounds play (muffled inside). mr.Weather.Kind / -MRWeatherKind
 *    (rain, snow, sand) overrides what the season brings (look-dev).
 *  - Atmosphere (phase 5; moods.json "atmosphere", AtmosphereFor): how dark it is (Night, from the
 *    sun's elevation), the season's tint on foliage and grass (Spring, Autumn, Winter) and a light
 *    snow lying outdoors in winter, how thickly the chimneys smoke (Smoke: more on cold mornings and
 *    in winter, less in a storm), and the ambient particles around the camera, by the zone profile's
 *    "ambient" kinds, the hour, the season and the storm (Motes, Pollen, Fireflies, Leaves). Moths
 *    circle the lamps while they're lit at night. mr.Env.Ambient 0 hides the ambient particles.
 *
 * -MRMood=<name> / mr.Env.Mood pins one mood (no cycle, the mood's own sun rotation): look-dev.
 * In editor worlds it only runs with mr.Env.Editor 1, so it never edits a level being worked on.
 * Console: MREnvReload re-reads moods.json.
 */
UCLASS()
class UNREALMERIDIAN_API UMREnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Sun / moon light direction for a game hour: (pitch, yaw) of a directional light. Elevation in
	    degrees > 0 when above the horizon. Pure, for tests. */
	static FRotator SkyBodyRotation(double Hour, double Rise, double Set, double MaxElevation, double& OutElevation);

	/** The original lamps' switch: 0 from 11 to 17, 1 otherwise, with short fades (kod lamp.kod). */
	static float LampsOnForHour(double Hour);

	/** The atmosphere for an hour (the sun's elevation in degrees), season (0 spring .. 3 winter),
	    storm amount (0..1) and place: Cfg is moods.json "atmosphere", ZoneAmbient the zone profile's
	    "ambient" ({kind: weight}). Pure, for tests. */
	static FMRAtmosphere AtmosphereFor(double Hour, double SunElevation, int32 Season, float Storm, bool bOutdoor,
		const TSharedPtr<FJsonObject>& Cfg, const TSharedPtr<FJsonObject>& ZoneAmbient);

	/** The original's room light (kod room.kod GetRoomLight) as 0..1: base light + outside factor *
	    (brightness - 50) / 4, clamped to 0..255, with the brightness interpolated between whole hours. */
	static double RoomLightForHour(double Hour, double BaseLight, double OutsideFactor);

	/** Re-read moods.json and apply on the next tick. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Environment")
	void Reload();

	/**
	 * Online the server says what falls in the room (BP_EFFECT EFFECT_RAINING, _SNOWING, _SAND, sent
	 * on entering a room and when its weather changes; docs/adr/0012 M4): it replaces the zone's own
	 * storm roll. bAlready: it was already falling (a room just entered), so it shows at once instead
	 * of building up; None clears it.
	 */
	void SetServerWeather(EMRWeatherKind Kind, bool bAlready);

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
	/** The zone the local view is in (camera location), else the local player's zone; -1 if none. */
	int32 LocalZoneId() const;
	/** A field of the zone's profile, else of the "default" profile. */
	TSharedPtr<FJsonValue> ProfileField(const TCHAR* Field) const;
	/** Storm amount, kind, wetness, snow cover for the zone the view is in (UpdateWeather). */
	void UpdateWeather();
	/** The state with the storm overlay mood blended in by the storm amount. */
	TSharedPtr<FJsonObject> WithStorm(const TSharedPtr<FJsonObject>& State);
	void UpdatePrecipitation(int32 Zone);
	/** Atmosphere (AtmosphereFor) for the hour, the zone and the weather. */
	void UpdateAtmosphere(double Hour);
	void UpdateAmbient(int32 Zone);
	AMRPrecipitationActor* EnsurePrecip();
	UMaterialInterface* ZoneMaterial(const TCHAR* Prefix, int32 Zone);
	void UpdateSounds();
	/** The original's file for a weather sound (moods.json "weather" "sounds"), played by UMRAudioSubsystem. */
	FString WeatherSoundFile(const TCHAR* Key) const;
	/** The clouds' material as a dynamic instance (made once), and a parameter's value before any mood set it. */
	class UMaterialInstanceDynamic* CloudMaterial();
	float CloudBase(const FName& Param);
	void TickLightning(float DeltaTime);
	bool IsOutdoor() const;

	TSharedPtr<FJsonObject> Root;
	/** moods.json "zones" entry for the zone the view is in ("default" if it has none). */
	TSharedPtr<FJsonObject> Profile;
	TMap<FString, TSharedPtr<FJsonObject>> Resolved;
	TMap<TWeakObjectPtr<ULightComponent>, float> LampBase;
	TSet<FString> Warned;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	FString PinnedMood;
	FString LastApplied;

	// weather (UpdateWeather)
	EMRWeatherKind WeatherKind = EMRWeatherKind::None;
	int32 WeatherMask = -1;
	bool bSoundsOn = true;  // UpdateSounds: the setting and the mask's sound bit
	float StormAmount = 0.f;
	float Wetness = 0.f;
	float SnowCover = 0.f;
	float WindAmount = 1.f;
	bool bStormy = false;
	FMRAtmosphere Atmosphere;
	FString LastAtmosphereLog;

	TWeakObjectPtr<UMaterialInstanceDynamic> CloudMID;
	TMap<FName, float> CloudBaseValues;

	TWeakObjectPtr<AMRPrecipitationActor> Precip;
	int32 PrecipZone = -1;
	int32 AmbientZone = -1;
	TMap<FString, TWeakObjectPtr<UMaterialInterface>> ZoneMaterials;
	FString KindOverride;
	/** SetServerWeather: whether the server decides, what it said, the last kind that fell and since when. */
	bool bServerWeather = false;
	EMRWeatherKind ServerWeather = EMRWeatherKind::None;
	EMRWeatherKind ServerWeatherLast = EMRWeatherKind::None;
	int64 ServerWeatherSince = 0;

	// sounds
	TArray<double> ThunderAt;
	TWeakObjectPtr<class UMaterialInstanceDynamic> BoltMID;

	// lightning
	TWeakObjectPtr<ULightComponent> LightningLight;
	double NextFlash = 0.0;
	double FlashStart = -1.0;
	float FlashScale = 1.f;
	float BaseWindowDaylight = 0.f;
	bool bNoLightning = false;
	bool bHoldLightning = false;  // -MRLightningHold: look-dev stills of a stroke
	double NextUpdate = 0.0;
	int32 LastZone = -1;
	double SkySliceRestoreAt = 0.0;  // when to time-slice the sky capture again after a zone change
	int32 SkySliceSaved = 1;
	bool bForce = true;
};
