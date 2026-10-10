#include "Environment/MRGameTimeSubsystem.h"

#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	const TCHAR* CollectionPath = TEXT("/Game/Generated/Environment/Materials/MPC_Environment.MPC_Environment");
	const FName GameHourParam(TEXT("GameHour"));

	constexpr double SecondsPerGameDay = 2.0 * 3600.0;  // a game day is two real hours
	constexpr double SecondsPerGameHour = SecondsPerGameDay / 24.0;  // five real minutes
	constexpr double ServerUtcOffset = -5.0 * 3600.0;  // the server assumes it runs 5 hours behind UTC

	constexpr int64 GameDaysPerYear = 240;  // kod system.kod NewGameDay: piDay = (piDay + 1) % 240

	TAutoConsoleVariable<float> CVarGameHour(
		TEXT("mr.GameHour"), -1.f,
		TEXT("Pin Meridian game time to this hour (0..24) for look-dev; < 0 follows the real clock."));

	TAutoConsoleVariable<int32> CVarSeason(
		TEXT("mr.Season"), -1,
		TEXT("Pin the Meridian season (0 spring, 1 summer, 2 fall, 3 winter) for look-dev; < 0 follows the clock."));
}

double UMRGameTimeSubsystem::GameHourAt(const FDateTime& Utc)
{
	const double Seconds = (Utc - FDateTime(1970, 1, 1)).GetTotalSeconds() + ServerUtcOffset;
	const double IntoDay = FMath::Fmod(FMath::Fmod(Seconds, SecondsPerGameDay) + SecondsPerGameDay, SecondsPerGameDay);
	return IntoDay / SecondsPerGameHour;
}

int64 UMRGameTimeSubsystem::GameDayCountAt(const FDateTime& Utc)
{
	const double Seconds = (Utc - FDateTime(1970, 1, 1)).GetTotalSeconds() + ServerUtcOffset;
	return int64(FMath::FloorToDouble(Seconds / SecondsPerGameDay));
}

int64 UMRGameTimeSubsystem::GameDayStartUnix(int64 GameDay)
{
	return int64(double(GameDay) * SecondsPerGameDay - ServerUtcOffset);
}

EMRDayPhase UMRGameTimeSubsystem::DayPhaseForHour(int32 Hour)
{
	// kod system.kod SysRecalcLightAndDayPhase, in its order
	if (Hour < 6 || Hour > 20)
	{
		return EMRDayPhase::Night;
	}
	if (Hour > 17)
	{
		return EMRDayPhase::Dusk;
	}
	if (Hour < 9)
	{
		return EMRDayPhase::Dawn;
	}
	return EMRDayPhase::Day;
}

int32 UMRGameTimeSubsystem::BrightnessForHour(int32 Hour)
{
	// kod: iLight_hour = (piHour - 2) % 24 (C-style remainder: -2 and -1 stay negative), then a
	// triangle over 24 hours peaking at light hour 12 (game hour 14), bounded to 15..75
	const int32 LightHour = (Hour - 2) % 24;
	const int32 Brightness = LightHour <= 12 ? LightHour * 100 / 12 : (24 - LightHour) * 100 / 12;
	return FMath::Clamp(Brightness, 15, 75);
}

EMRDayPhase UMRGameTimeSubsystem::GetDayPhase() const
{
	return DayPhaseForHour(FMath::FloorToInt32(GetGameHour()));
}

int32 UMRGameTimeSubsystem::GetBrightness() const
{
	return BrightnessForHour(FMath::FloorToInt32(GetGameHour()));
}

int32 UMRGameTimeSubsystem::GetSeason() const
{
	const int32 CVar = CVarSeason.GetValueOnGameThread();
	if (CVar >= 0)
	{
		return CVar % 4;
	}
	if (PinnedSeason >= 0)
	{
		return PinnedSeason;
	}
	const int64 Year = FMath::FloorToInt64(double(GameDayCountAt(FDateTime::UtcNow())) / double(GameDaysPerYear));
	return int32(((Year % 4) + 4) % 4);
}

double UMRGameTimeSubsystem::GetGameHour() const
{
	const float CVar = CVarGameHour.GetValueOnGameThread();
	if (CVar >= 0.f)
	{
		return FMath::Fmod(double(CVar), 24.0);
	}
	if (PinnedHour >= 0.0)
	{
		return PinnedHour;
	}
	if (StartHour >= 0.0)
	{
		return FMath::Fmod(StartHour + (FPlatformTime::Seconds() - StartedAt) / SecondsPerGameHour, 24.0);
	}
	return GameHourAt(FDateTime::UtcNow());
}

void UMRGameTimeSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	float Hour = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRGameHour="), Hour) && Hour >= 0.f)
	{
		PinnedHour = FMath::Fmod(double(Hour), 24.0);
	}
	// -MRGameHourFrom=<h>: start at that hour and let the clock run (the hitch test: the sun keeps moving)
	if (FParse::Value(FCommandLine::Get(), TEXT("MRGameHourFrom="), Hour) && Hour >= 0.f)
	{
		StartHour = FMath::Fmod(double(Hour), 24.0);
		StartedAt = FPlatformTime::Seconds();
	}
	int32 Season = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRSeason="), Season) && Season >= 0)
	{
		PinnedSeason = Season % 4;
	}
}

void UMRGameTimeSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!Collection || !World)
	{
		return;
	}
	const double Hour = GetGameHour();
	if (FMath::Abs(Hour - LastWritten) > 1e-3)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, GameHourParam, float(Hour));
		LastWritten = Hour;
	}
}

TStatId UMRGameTimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRGameTimeSubsystem, STATGROUP_Tickables);
}

bool UMRGameTimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}
