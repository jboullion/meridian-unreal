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

	TAutoConsoleVariable<float> CVarGameHour(
		TEXT("mr.GameHour"), -1.f,
		TEXT("Pin Meridian game time to this hour (0..24) for look-dev; < 0 follows the real clock."));
}

double UMRGameTimeSubsystem::GameHourAt(const FDateTime& Utc)
{
	const double Seconds = (Utc - FDateTime(1970, 1, 1)).GetTotalSeconds() + ServerUtcOffset;
	const double IntoDay = FMath::Fmod(FMath::Fmod(Seconds, SecondsPerGameDay) + SecondsPerGameDay, SecondsPerGameDay);
	return IntoDay / SecondsPerGameHour;
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
