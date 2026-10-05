// Automation tests for the game clock and the environment director's pure parts (docs/adr/0005).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Environment;Quit" -unattended -nullrhi

#include "Environment/MREnvironmentSubsystem.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "Misc/AutomationTest.h"

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

#endif
