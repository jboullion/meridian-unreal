#pragma once

#include "CoreMinimal.h"
#include "MRWeather.generated.h"

/** What a storm brings to a room (kod room.kod StartStorm: sand before snow before rain). */
UENUM(BlueprintType)
enum class EMRWeatherKind : uint8
{
	None = 0,
	Rain = 1,
	Snow = 2,
	Sand = 3
};

/** One weather zone's state as the server replicates it (docs/adr/0005 §5). */
USTRUCT(BlueprintType)
struct UNREALMERIDIAN_API FMRZoneWeather
{
	GENERATED_BODY()

	/** kod WEATHER_ZONE_* (1..15). */
	UPROPERTY(BlueprintReadOnly)
	uint8 Zone = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bStorm = false;

	/** When the zone last changed between clear and storm (Unix seconds, UTC); 0 = never (dry). */
	UPROPERTY(BlueprintReadOnly)
	int64 SinceUnix = 0;
};

/**
 * The original server's weather rules (kod util/system.kod RecalcWeatherConditions, room.kod
 * StartStorm), as pure functions so the server, the client and the tests share them:
 *  - every game day (2 real hours) each weather zone rolls a storm with the storm chance (15%,
 *    kod settings.kod piStormChance); the original rolls with Random(), here the roll is a hash of
 *    the game day, the zone and a seed, so a day's weather is reproducible;
 *  - a room's weather mask says what a storm brings in each season (kod WEATHER_MASK_*).
 * The client-side curves (how fast a storm builds, how long the ground stays wet) are ours.
 */
namespace MRWeather
{
	constexpr int32 MaxZone = 15;  // kod WEATHER_ZONE_MAX
	constexpr int32 DefaultStormChance = 15;

	/** True if the zone storms on that game day. */
	UNREALMERIDIAN_API bool RollStorm(int64 GameDay, int32 Zone, int32 Seed, int32 ChancePercent);

	/** The game day the zone's current run of clear or stormy days began (looks back at most 60 days). */
	UNREALMERIDIAN_API int64 RunStartDay(int64 GameDay, int32 Zone, int32 Seed, int32 ChancePercent);

	/** kod mask names without the WEATHER_MASK_ prefix (DEFAULT_NS, DESERT, TUNDRA...) -> the mask; -1 if unknown. */
	UNREALMERIDIAN_API int32 MaskByName(const FString& Name);

	/** What a storm brings with this mask in this season (0 spring .. 3 winter): sand, else snow, else rain. */
	UNREALMERIDIAN_API EMRWeatherKind KindFor(int32 Mask, int32 Season);

	/** How much of the storm is there (0..1), SecondsSince the change: builds over RampIn, clears over RampOut. */
	UNREALMERIDIAN_API float StormAmount(bool bStorm, double SecondsSince, double RampIn = 90.0, double RampOut = 120.0);

	/** How wet (rain) or white (snow) the ground is (0..1): builds over BuildSeconds once it falls,
	    fades over FadeSeconds after it stops (from full: a storm lasts at least a game day, 2 hours). */
	UNREALMERIDIAN_API float Cover(bool bFalling, double SecondsSince, double BuildSeconds, double FadeSeconds);
}
