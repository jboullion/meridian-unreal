#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRGameTimeSubsystem.generated.h"

class UMaterialParameterCollection;

/** The original day phases (kod blakston.khd DAY_PHASE_*). */
UENUM(BlueprintType)
enum class EMRDayPhase : uint8
{
	Dawn = 0,
	Day = 1,
	Dusk = 2,
	Night = 3
};

/**
 * Meridian game time, as the original server keeps it (kod util/system.kod SystemInitGameHour):
 * a game day lasts two real hours, so a game hour is five real minutes, and the hour follows from
 * UTC alone - ((UTC - 5 h) mod 2 h) / 5 min - so every client agrees without asking the server.
 *
 * Each tick it writes the hour (fractional, 0..24) to MPC_Environment.GameHour, which the clock
 * faces read to show the hour the original server would (frame hour mod 12, kod raza.kod).
 * -MRGameHour=<h> on the command line, or the console variable mr.GameHour (>= 0), pins it (look-dev).
 *
 * The day phase and brightness are the original server's (SysRecalcLightAndDayPhase), from the whole
 * game hour. The original counts game days and years on the server (NewGameDay, NewGameYear: a year
 * is 240 game days, the season is year mod 4); here they also follow from UTC, like the hour, so
 * every client agrees (docs/adr/0005). -MRSeason=<0..3> / mr.Season pins the season.
 */
UCLASS()
class UNREALMERIDIAN_API UMRGameTimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Game hour (0..24, fractional) for a moment in UTC. */
	static double GameHourAt(const FDateTime& Utc);

	/** Whole game days since the epoch for a moment in UTC (the day changes when the hour wraps). */
	static int64 GameDayCountAt(const FDateTime& Utc);

	/** When a game day (GameDayCountAt) begins, in Unix seconds (UTC). */
	static int64 GameDayStartUnix(int64 GameDay);

	/** The original's day phase for a whole game hour: night < 6 or > 20, dawn < 9, dusk > 17. */
	static EMRDayPhase DayPhaseForHour(int32 Hour);

	/** The original's sky brightness (15..75) for a whole game hour: peaks from 11 to 17, low at night. */
	static int32 BrightnessForHour(int32 Hour);

	/** The current game hour (0..24, fractional), or the pinned one. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Time")
	double GetGameHour() const;

	UFUNCTION(BlueprintCallable, Category = "Meridian|Time")
	EMRDayPhase GetDayPhase() const;

	/** 15..75, as the original server's SystemGetBrightness. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Time")
	int32 GetBrightness() const;

	/** 0 spring, 1 summer, 2 fall, 3 winter (kod WEATHER_SEASON_*); a season lasts a game year (240 game days). */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Time")
	int32 GetSeason() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	double PinnedHour = -1.0;
	double StartHour = -1.0;  // -MRGameHourFrom
	double StartedAt = 0.0;
	int32 PinnedSeason = -1;
	double LastWritten = -1.0;
};
