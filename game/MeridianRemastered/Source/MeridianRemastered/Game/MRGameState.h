#pragma once

#include "CoreMinimal.h"
#include "Environment/MRWeather.h"
#include "GameFramework/GameStateBase.h"
#include "MRGameState.generated.h"

/**
 * Replicated world state. Weather (docs/adr/0005 §5): the server keeps one entry per weather zone
 * (kod WEATHER_ZONE_*, 1..15): storm or clear, and since when. It re-rolls when the game day changes,
 * as the original server does on NewGameDay; the roll is MRWeather::RollStorm, so a day's weather is
 * reproducible (mr.Weather.Seed) and a server started mid-storm reports the storm from the start of
 * its run of days. A few bytes every 2 hours. Clients blend from SinceUnix, so a player who logs in
 * mid-storm sees it at full strength.
 *
 * Overrides (server): -MRWeather=storm|clear on the command line (look-dev: at full strength from the
 * start), or the console variable mr.Weather storm|clear (builds up from now); empty follows the rolls.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AMRGameState();

	/** The weather of a weather zone (0 = none: always clear, dry). */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Weather")
	FMRZoneWeather GetZoneWeather(int32 WeatherZone) const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void Recompute(int64 NowUnix);

	UPROPERTY(Replicated)
	TArray<FMRZoneWeather> Weather;

	int64 LastDay = MIN_int64;
	FString LastOverride;
	int64 OverrideSince = 0;
	FString CommandLineOverride;
};
