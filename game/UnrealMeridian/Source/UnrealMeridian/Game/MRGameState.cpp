#include "Game/MRGameState.h"

#include "Environment/MRGameTimeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
	TAutoConsoleVariable<FString> CVarWeather(
		TEXT("mr.Weather"), TEXT(""),
		TEXT("Server: storm or clear in every weather zone, building up from now; empty follows the daily rolls."));
	TAutoConsoleVariable<int32> CVarSeed(
		TEXT("mr.Weather.Seed"), 59, TEXT("Server: the seed of the daily storm rolls (the same seed gives the same weather)."));
	TAutoConsoleVariable<int32> CVarChance(
		TEXT("mr.Weather.StormChance"), MRWeather::DefaultStormChance,
		TEXT("Server: percent chance of a storm per weather zone per game day (kod settings.kod piStormChance)."));

	int64 UnixNow()
	{
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
	}
}

AMRGameState::AMRGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
}

void AMRGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		FParse::Value(FCommandLine::Get(), TEXT("MRWeather="), CommandLineOverride);
		Recompute(UnixNow());
	}
}

void AMRGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	const int64 Now = UnixNow();
	const int64 Day = UMRGameTimeSubsystem::GameDayCountAt(FDateTime::UtcNow());
	if (Day != LastDay || CVarWeather.GetValueOnGameThread() != LastOverride)
	{
		Recompute(Now);
	}
}

void AMRGameState::Recompute(int64 NowUnix)
{
	const int64 Day = UMRGameTimeSubsystem::GameDayCountAt(FDateTime::UtcNow());
	const FString CVar = CVarWeather.GetValueOnGameThread().ToLower();
	if (CVar != LastOverride.ToLower())
	{
		OverrideSince = NowUnix;  // a console override builds up from now
	}
	LastOverride = CVarWeather.GetValueOnGameThread();
	LastDay = Day;

	FString Override = CVar;
	int64 Since = OverrideSince;
	if (Override.IsEmpty() && !CommandLineOverride.IsEmpty())
	{
		Override = CommandLineOverride.ToLower();
		Since = NowUnix - 86400;  // look-dev: as if it had been going for a long time
	}
	const int32 Seed = CVarSeed.GetValueOnGameThread();
	const int32 Chance = CVarChance.GetValueOnGameThread();

	TArray<FMRZoneWeather> Next;
	for (int32 Zone = 1; Zone <= MRWeather::MaxZone; ++Zone)
	{
		FMRZoneWeather W;
		W.Zone = uint8(Zone);
		if (Override == TEXT("storm") || Override == TEXT("clear"))
		{
			W.bStorm = Override == TEXT("storm");
			W.SinceUnix = Since;
		}
		else
		{
			W.bStorm = MRWeather::RollStorm(Day, Zone, Seed, Chance);
			const int64 Start = MRWeather::RunStartDay(Day, Zone, Seed, Chance);
			// a run of clear days that goes back past the look-back never followed a storm: dry
			const bool bSinceForever = !W.bStorm && Start <= Day - 59;
			W.SinceUnix = bSinceForever ? 0 : UMRGameTimeSubsystem::GameDayStartUnix(Start);
		}
		Next.Add(W);
	}
	for (int32 i = 0; i < Next.Num(); ++i)
	{
		if (!Weather.IsValidIndex(i) || Weather[i].bStorm != Next[i].bStorm)
		{
			UE_LOG(LogMeridian, Log, TEXT("Weather: zone %d %s"), Next[i].Zone, Next[i].bStorm ? TEXT("storm") : TEXT("clear"));
		}
	}
	Weather = MoveTemp(Next);
}

FMRZoneWeather AMRGameState::GetZoneWeather(int32 WeatherZone) const
{
	for (const FMRZoneWeather& W : Weather)
	{
		if (W.Zone == WeatherZone)
		{
			return W;
		}
	}
	FMRZoneWeather None;
	None.Zone = uint8(FMath::Clamp(WeatherZone, 0, 255));
	return None;
}

void AMRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMRGameState, Weather);
}
