// Automation tests for the game clock and the environment director's pure parts (docs/adr/0005).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Environment;Quit" -unattended -nullrhi

#include "Environment/MREnvironmentSubsystem.h"
#include "Environment/MRFireSubsystem.h"
#include "Environment/MRWeather.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRGameTimeTest, "Meridian.Environment.GameTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRGameTimeTest::RunTest(const FString& Parameters)
{
	// the server assumes it runs 5 h behind UTC: 05:00 UTC is game midnight, and a game hour is 5 minutes
	TestEqual(TEXT("05:00 UTC is hour 0"), UMRGameTimeSubsystem::GameHourAt(FDateTime(2026, 10, 5, 5, 0, 0)), 0.0, 1e-6);
	TestEqual(TEXT("05:30 UTC is hour 6"), UMRGameTimeSubsystem::GameHourAt(FDateTime(2026, 10, 5, 5, 30, 0)), 6.0, 1e-6);
	TestEqual(TEXT("a game day is two hours"), UMRGameTimeSubsystem::GameHourAt(FDateTime(2026, 10, 5, 7, 30, 0)), 6.0, 1e-6);
	TestEqual(TEXT("the day count steps when the hour wraps"),
		UMRGameTimeSubsystem::GameDayCountAt(FDateTime(2026, 10, 5, 7, 0, 0)) - UMRGameTimeSubsystem::GameDayCountAt(FDateTime(2026, 10, 5, 6, 59, 59)),
		int64(1));

	// kod system.kod SysRecalcLightAndDayPhase
	const struct { int32 Hour; EMRDayPhase Phase; int32 Brightness; } Table[] = {
		{0, EMRDayPhase::Night, 15}, {2, EMRDayPhase::Night, 15}, {5, EMRDayPhase::Night, 25},
		{6, EMRDayPhase::Dawn, 33}, {8, EMRDayPhase::Dawn, 50}, {9, EMRDayPhase::Day, 58},
		{11, EMRDayPhase::Day, 75}, {14, EMRDayPhase::Day, 75}, {17, EMRDayPhase::Day, 75},
		{18, EMRDayPhase::Dusk, 66}, {20, EMRDayPhase::Dusk, 50}, {21, EMRDayPhase::Night, 41}, {23, EMRDayPhase::Night, 25},
	};
	for (const auto& Row : Table)
	{
		TestEqual(FString::Printf(TEXT("phase at %d"), Row.Hour), int32(UMRGameTimeSubsystem::DayPhaseForHour(Row.Hour)), int32(Row.Phase));
		TestEqual(FString::Printf(TEXT("brightness at %d"), Row.Hour), UMRGameTimeSubsystem::BrightnessForHour(Row.Hour), Row.Brightness);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSkyPathTest, "Meridian.Environment.SkyPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRSkyPathTest::RunTest(const FString& Parameters)
{
	double Elevation = 0.0;
	UMREnvironmentSubsystem::SkyBodyRotation(6.0, 6.0, 22.0, 50.0, Elevation);
	TestEqual(TEXT("the sun rises at 6"), Elevation, 0.0, 1e-6);
	UMREnvironmentSubsystem::SkyBodyRotation(14.0, 6.0, 22.0, 50.0, Elevation);
	TestEqual(TEXT("the sun is highest at 14, where the original's brightness peaks"), Elevation, 50.0, 1e-6);
	const FRotator Afternoon = UMREnvironmentSubsystem::SkyBodyRotation(17.0, 6.0, 22.0, 50.0, Elevation);
	TestTrue(TEXT("at 17 the sun is in the south-west, as the tuned afternoon mood"),
		FMath::Abs(FRotator::NormalizeAxis(Afternoon.Yaw) - (-55.0)) < 10.0 && Elevation > 35.0 && Elevation < 45.0);
	UMREnvironmentSubsystem::SkyBodyRotation(2.0, 6.0, 22.0, 50.0, Elevation);
	TestTrue(TEXT("the sun is down at 2"), Elevation < 0.0);
	UMREnvironmentSubsystem::SkyBodyRotation(2.0, 18.0, 34.0, 40.0, Elevation);
	TestEqual(TEXT("the moon is highest at 2"), Elevation, 40.0, 1e-6);

	TestEqual(TEXT("lamps on at night"), UMREnvironmentSubsystem::LampsOnForHour(2.0), 1.0f);
	TestEqual(TEXT("lamps off at noon"), UMREnvironmentSubsystem::LampsOnForHour(14.0), 0.0f);
	TestEqual(TEXT("lamps still off at 17:59 (kod: off while hour <= 17)"), UMREnvironmentSubsystem::LampsOnForHour(17.99), 0.0f);
	TestEqual(TEXT("lamps on by 18:15"), UMREnvironmentSubsystem::LampsOnForHour(18.25), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRFlickerTest, "Meridian.Environment.Flicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRFlickerTest::RunTest(const FString& Parameters)
{
	// fire light flicker (docs/adr/0005 phase 3): bounded, deterministic, lively, and different per seed
	double Sum = 0.0, SumSq = 0.0, Cross = 0.0, SumB = 0.0, SumSqB = 0.0;
	const int32 N = 6000;
	bool bInRange = true;
	for (int32 i = 0; i < N; ++i)
	{
		const double T = i * 0.01;
		const float A = UMRFireSubsystem::FlickerAt(T, 1234);
		const float B = UMRFireSubsystem::FlickerAt(T, 5678);
		bInRange &= A >= -1.f && A <= 1.f && B >= -1.f && B <= 1.f;
		Sum += A; SumSq += A * A; SumB += B; SumSqB += B * B; Cross += A * B;
	}
	TestTrue(TEXT("flicker stays within -1..1"), bInRange);
	TestEqual(TEXT("the same seed and time give the same flicker"), UMRFireSubsystem::FlickerAt(12.345, 77), UMRFireSubsystem::FlickerAt(12.345, 77));
	const double Mean = Sum / N, MeanB = SumB / N;
	const double Std = FMath::Sqrt(SumSq / N - Mean * Mean), StdB = FMath::Sqrt(SumSqB / N - MeanB * MeanB);
	TestTrue(FString::Printf(TEXT("flicker averages out (mean %.3f)"), Mean), FMath::Abs(Mean) < 0.1);
	TestTrue(FString::Printf(TEXT("flicker moves (std %.3f)"), Std), Std > 0.2);
	const double Corr = (Cross / N - Mean * MeanB) / (Std * StdB);
	TestTrue(FString::Printf(TEXT("two fires don't pulse together (correlation %.3f)"), Corr), FMath::Abs(Corr) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRWeatherTest, "Meridian.Environment.Weather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRWeatherTest::RunTest(const FString& Parameters)
{
	// the original's rules (kod system.kod RecalcWeatherConditions, room.kod StartStorm)
	int32 Storms = 0;
	const int32 Days = 20000;
	for (int64 Day = 0; Day < Days; ++Day)
	{
		Storms += MRWeather::RollStorm(Day, 13, 59, 15) ? 1 : 0;
	}
	const double Rate = double(Storms) / Days;
	TestTrue(FString::Printf(TEXT("about 15%% of game days storm (%.3f)"), Rate), Rate > 0.13 && Rate < 0.17);
	TestEqual(TEXT("a roll is reproducible"), MRWeather::RollStorm(12345, 13, 59, 15), MRWeather::RollStorm(12345, 13, 59, 15));
	int32 Differ = 0;
	for (int64 Day = 0; Day < 2000; ++Day)
	{
		Differ += MRWeather::RollStorm(Day, 13, 59, 15) != MRWeather::RollStorm(Day, 4, 59, 15) ? 1 : 0;
	}
	TestTrue(TEXT("zones roll independently"), Differ > 300);
	TestFalse(TEXT("chance 0 never storms"), MRWeather::RollStorm(7, 13, 59, 0));
	TestTrue(TEXT("chance 100 always storms"), MRWeather::RollStorm(7, 13, 59, 100));
	for (int64 Day = 100; Day < 400; ++Day)
	{
		const int64 Start = MRWeather::RunStartDay(Day, 13, 59, 15);
		const bool bStorm = MRWeather::RollStorm(Day, 13, 59, 15);
		if (Start > Day || MRWeather::RollStorm(Start, 13, 59, 15) != bStorm || (Start > Day - 60 && MRWeather::RollStorm(Start - 1, 13, 59, 15) == bStorm))
		{
			AddError(FString::Printf(TEXT("run start of day %lld is %lld"), Day, Start));
			break;
		}
	}

	// Raza's mask (WEATHER_MASK_DEFAULT_NS): rain in spring, summer and fall, snow in winter
	const int32 Raza = MRWeather::MaskByName(TEXT("DEFAULT_NS"));
	TestEqual(TEXT("DEFAULT_NS"), Raza, 0x21110);
	TestEqual(TEXT("spring rain"), int32(MRWeather::KindFor(Raza, 0)), int32(EMRWeatherKind::Rain));
	TestEqual(TEXT("summer rain"), int32(MRWeather::KindFor(Raza, 1)), int32(EMRWeatherKind::Rain));
	TestEqual(TEXT("fall rain"), int32(MRWeather::KindFor(Raza, 2)), int32(EMRWeatherKind::Rain));
	TestEqual(TEXT("winter snow"), int32(MRWeather::KindFor(Raza, 3)), int32(EMRWeatherKind::Snow));
	TestEqual(TEXT("desert sand"), int32(MRWeather::KindFor(MRWeather::MaskByName(TEXT("DESERT")), 1)), int32(EMRWeatherKind::Sand));
	TestEqual(TEXT("mountains snow in fall"), int32(MRWeather::KindFor(MRWeather::MaskByName(TEXT("MOUNTAINS")), 2)), int32(EMRWeatherKind::Snow));
	TestEqual(TEXT("no mask, no weather"), int32(MRWeather::KindFor(MRWeather::MaskByName(TEXT("NONE")), 1)), int32(EMRWeatherKind::None));
	TestEqual(TEXT("unknown mask"), MRWeather::MaskByName(TEXT("nonsense")), -1);

	// the client's curves
	TestEqual(TEXT("a storm starts at nothing"), MRWeather::StormAmount(true, 0.0), 0.f);
	TestEqual(TEXT("a storm is full after its build"), MRWeather::StormAmount(true, 90.0), 1.f);
	TestEqual(TEXT("a cleared storm is gone"), MRWeather::StormAmount(false, 120.0), 0.f);
	TestEqual(TEXT("clear long ago: nothing"), MRWeather::StormAmount(false, 1.0e9), 0.f);
	TestEqual(TEXT("half wet"), MRWeather::Cover(true, 90.0, 180.0, 600.0), 0.5f, 1e-4f);
	TestEqual(TEXT("dry after the fade"), MRWeather::Cover(false, 600.0, 180.0, 600.0), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAtmosphereTest, "Meridian.Environment.Atmosphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRAtmosphereTest::RunTest(const FString& Parameters)
{
	auto Parse = [](const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Obj;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Obj);
		return Obj;
	};
	// as moods.json "atmosphere"
	const TSharedPtr<FJsonObject> Cfg = Parse(TEXT("{\"night_deg\": [-4, 6], \"seasons\": {\"tint\": [1, 0, 1, 1], \"winter_snow_cover\": 0.12},")
		TEXT("\"smoke\": {\"base\": 0.45, \"morning\": 0.35, \"morning_hours\": [4, 6, 9, 11], \"night\": 0.1, \"winter\": 0.35, \"storm\": 0.6},")
		TEXT("\"ambient\": {\"motes\": {\"seasons\": [1, 1, 1, 1], \"when\": \"always\"},")
		TEXT("\"pollen\": {\"seasons\": [1, 0.7, 0.2, 0], \"when\": \"day\", \"storm\": 1},")
		TEXT("\"fireflies\": {\"seasons\": [0.4, 1, 0.3, 0], \"when\": \"night\", \"storm\": 1},")
		TEXT("\"leaves\": {\"seasons\": [0.15, 0.25, 1, 0.05], \"when\": \"always\", \"storm\": -1}}}"));
	const TSharedPtr<FJsonObject> Town = Parse(TEXT("{\"pollen\": 1, \"fireflies\": 1}"));
	const TSharedPtr<FJsonObject> Forest = Parse(TEXT("{\"pollen\": 1, \"fireflies\": 1, \"leaves\": 1}"));
	const TSharedPtr<FJsonObject> Inn = Parse(TEXT("{\"motes\": 1}"));

	const FMRAtmosphere Noon = UMREnvironmentSubsystem::AtmosphereFor(14.0, 50.0, 1, 0.f, true, Cfg, Town);
	TestEqual(TEXT("noon is day"), Noon.Night, 0.f);
	TestEqual(TEXT("summer pollen by day"), Noon.Pollen, 0.7f, 1e-4f);
	TestEqual(TEXT("no fireflies by day"), Noon.Fireflies, 0.f);
	TestEqual(TEXT("no motes outdoors"), Noon.Motes, 0.f);
	TestEqual(TEXT("summer: the original colours"), Noon.Spring + Noon.Autumn + Noon.Winter, 0.f);
	TestEqual(TEXT("an afternoon's smoke is the base"), Noon.Smoke, 0.45f, 1e-4f);

	const FMRAtmosphere Midnight = UMREnvironmentSubsystem::AtmosphereFor(2.0, -20.0, 1, 0.f, true, Cfg, Town);
	TestEqual(TEXT("midnight is night"), Midnight.Night, 1.f);
	TestEqual(TEXT("summer fireflies at night"), Midnight.Fireflies, 1.f);
	TestEqual(TEXT("no pollen at night"), Midnight.Pollen, 0.f);
	const FMRAtmosphere Dusk = UMREnvironmentSubsystem::AtmosphereFor(20.0, 1.0, 1, 0.f, true, Cfg, Town);
	TestTrue(TEXT("dusk is half dark"), Dusk.Night > 0.2f && Dusk.Night < 0.8f);

	const FMRAtmosphere Storm = UMREnvironmentSubsystem::AtmosphereFor(2.0, -20.0, 1, 1.f, true, Cfg, Forest);
	TestEqual(TEXT("no fireflies in a storm"), Storm.Fireflies, 0.f);
	TestEqual(TEXT("more leaves in a storm"), Storm.Leaves, 0.5f, 1e-4f);
	TestTrue(TEXT("less smoke in a storm"), Storm.Smoke < Midnight.Smoke);

	const FMRAtmosphere Morning = UMREnvironmentSubsystem::AtmosphereFor(7.0, 10.0, 3, 0.f, true, Cfg, Forest);
	TestEqual(TEXT("winter tint"), Morning.Winter, 1.f);
	TestEqual(TEXT("snow lies in winter"), Morning.WinterCover, 0.12f, 1e-4f);
	TestEqual(TEXT("a cold winter morning smokes fully"), Morning.Smoke, 1.f);
	TestEqual(TEXT("no fireflies in winter"), UMREnvironmentSubsystem::AtmosphereFor(2.0, -20.0, 3, 0.f, true, Cfg, Town).Fireflies, 0.f);
	TestEqual(TEXT("autumn tint"), UMREnvironmentSubsystem::AtmosphereFor(14.0, 50.0, 2, 0.f, true, Cfg, Forest).Autumn, 1.f);
	TestEqual(TEXT("autumn leaves"), UMREnvironmentSubsystem::AtmosphereFor(14.0, 50.0, 2, 0.f, true, Cfg, Forest).Leaves, 1.f);

	const FMRAtmosphere Inside = UMREnvironmentSubsystem::AtmosphereFor(14.0, 50.0, 3, 1.f, false, Cfg, Inn);
	TestEqual(TEXT("motes inside, storm or not"), Inside.Motes, 1.f);
	TestEqual(TEXT("no snow lies inside"), Inside.WinterCover, 0.f);
	TestEqual(TEXT("no pollen inside"), Inside.Pollen, 0.f);
	return true;
}

#endif
