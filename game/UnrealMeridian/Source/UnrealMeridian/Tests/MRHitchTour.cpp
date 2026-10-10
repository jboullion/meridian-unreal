#include "Tests/MRHitchTour.h"

#include "Character/MRCharacter.h"
#include "Core/MRWarmup.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "UnrealMeridian.h"

namespace
{
	constexpr float SpinSeconds = 4.f;   // a full circle at each stop
	constexpr float WalkSeconds = 1.5f;  // then a few steps forward
	constexpr float EyeAboveCentreCm = 82.f;  // the 1.65 m eye over the 162 cm capsule's centre (2 cm float)

	bool WorldReady(UWorld* World)
	{
		const UMRWarmup* Warmup = World ? World->GetSubsystem<UMRWarmup>() : nullptr;
		return Warmup ? Warmup->IsReady() : World && World->GetRealTimeSeconds() > 2.0;
	}
}

bool UMRHitchTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRHitchTour"));
}

void UMRHitchTour::Start(APlayerController* InController)
{
	Controller = InController;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MRHitchLabel="), Label) || Label.IsEmpty())
	{
		Label = TEXT("latest");
	}
	// uncapped by default, so a hitch shows as a long frame rather than hiding in a frame cap;
	// -MRHitchMaxFPS=60 plays capped, as most players do
	int32 MaxFps = 0;
	FParse::Value(FCommandLine::Get(), TEXT("MRHitchMaxFPS="), MaxFps);
	InController->ConsoleCommand(TEXT("r.VSync 0"));
	InController->ConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"), FMath::Max(0, MaxFps)));
	// -MRHitchCsv: a CSV profile with per-pass GPU timings (Saved/Profiling/CSV/), for finding what a spike is
	bCsv = FParse::Param(FCommandLine::Get(), TEXT("MRHitchCsv"));
	if (bCsv)
	{
		InController->ConsoleCommand(TEXT("r.GPUCsvStatsEnabled 1"));
		InController->ConsoleCommand(TEXT("CsvProfile Start"));
	}
	// eye positions in world cm (data/environment/lookdev_cameras.json), first stop: where the game spawns us
	Stops = {
		{TEXT("spawn"), FVector::ZeroVector, 0.f},
		{TEXT("inn_front"), FVector(5750, 2500, 200), -92.f},
		{TEXT("hall_front"), FVector(6150, 4500, 205), 36.f},
		{TEXT("pond"), FVector(6900, 4750, 200), -48.f},
		{TEXT("shops_row"), FVector(9250, 2500, 200), 38.f},
		{TEXT("north_gate"), FVector(8220, 1700, 205), -90.f},
		{TEXT("outskirts"), FVector(8220, -600, 205), -90.f},
		{TEXT("west_street"), FVector(4100, 4000, 205), 180.f},
		{TEXT("tavern_front"), FVector(3400, 3100, 200), 178.f},
		{TEXT("int_hall"), FVector(400378, 428, 170), 50.f},
		{TEXT("int_crypt"), FVector(402180, 202370, 1614), 0.f},
		{TEXT("int_bar"), FVector(600435, 200455, 170), 48.f},
		{TEXT("square_again"), FVector(6150, 4500, 205), 36.f},
	};
	Frames.Reserve(20000);
	StartTime = LastFrameTime = FPlatformTime::Seconds();
	StopIndex = -1;
	bRunning = true;
}

TStatId UMRHitchTour::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRHitchTour, STATGROUP_Tickables);
}

void UMRHitchTour::Tick(float DeltaTime)
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		bRunning = false;
		return;
	}
	const double Now = FPlatformTime::Seconds();
	FFrame& F = Frames.AddDefaulted_GetRef();
	F.Time = Now - StartTime;
	F.FrameMs = float((Now - LastFrameTime) * 1000.0);
	F.GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	F.RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	F.GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	F.Stop = StopIndex;
	LastFrameTime = Now;

	if (StopIndex < 0)
	{
		if (!WorldReady(PC->GetWorld()) || !PC->GetPawn())
		{
			return;
		}
		WarmupSeconds = Now - StartTime;
		UE_LOG(LogMeridian, Display, TEXT("MRHitch: ready after %.1f s"), WarmupSeconds);
		StopIndex = 0;
		BeginStop();
		return;
	}
	APawn* Pawn = PC->GetPawn();
	const float T = float(Now - StopStart);
	if (T < SpinSeconds)
	{
		FRotator R = PC->GetControlRotation();
		R.Yaw = Stops[StopIndex].Yaw + 360.f * T / SpinSeconds;
		R.Pitch = 0.f;
		PC->SetControlRotation(R);
	}
	else if (T < SpinSeconds + WalkSeconds)
	{
		if (Pawn)
		{
			Pawn->AddMovementInput(PC->GetControlRotation().Vector().GetSafeNormal2D(), 1.f);
		}
	}
	else if (++StopIndex < Stops.Num())
	{
		BeginStop();
	}
	else
	{
		Finish();
	}
}

void UMRHitchTour::BeginStop()
{
	APlayerController* PC = Controller.Get();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FStop& S = Stops[StopIndex];
	if (Pawn && !S.Eye.IsZero())
	{
		Pawn->TeleportTo(S.Eye - FVector(0.f, 0.f, EyeAboveCentreCm), FRotator(0.f, S.Yaw, 0.f));
	}
	if (PC)
	{
		const float Yaw = S.Eye.IsZero() ? PC->GetControlRotation().Yaw : S.Yaw;
		Stops[StopIndex].Yaw = Yaw;
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
	}
	StopStart = FPlatformTime::Seconds();
	// -MRHitchCmdAt=<stop>:<console command>: e.g. north_gate:ProfileGPU, for a closer look at one place
	FString CmdAt;
	if (PC && FParse::Value(FCommandLine::Get(), TEXT("MRHitchCmdAt="), CmdAt, false))
	{
		FString Stop, Cmd;
		if (CmdAt.Split(TEXT(":"), &Stop, &Cmd) && Stop == S.Name)
		{
			PC->ConsoleCommand(Cmd);
		}
	}
}

void UMRHitchTour::Finish()
{
	bRunning = false;
	// the CSV: every frame, so a spike's place and time can be found
	FString Csv = TEXT("time_s,frame_ms,game_ms,render_ms,gpu_ms,stop\n");
	for (const FFrame& F : Frames)
	{
		Csv += FString::Printf(TEXT("%.3f,%.2f,%.2f,%.2f,%.2f,%s\n"), F.Time, F.FrameMs, F.GameMs, F.RenderMs, F.GpuMs,
			Stops.IsValidIndex(F.Stop) ? *Stops[F.Stop].Name : TEXT("warmup"));
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MRHitch"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = FPaths::Combine(Dir, Label + TEXT(".csv"));
	FFileHelper::SaveStringToFile(Csv, *Path);

	// the summary covers the tour, not the warm-up (the player is waiting behind the loading screen then)
	TArray<float> Times;
	TArray<int32> Over33, Over100;
	Over33.SetNumZeroed(Stops.Num());
	Over100.SetNumZeroed(Stops.Num());
	float Worst = 0.f;
	for (int32 i = 1; i < Frames.Num(); ++i)  // the first frame measures nothing
	{
		const FFrame& F = Frames[i];
		if (F.Stop < 0)
		{
			continue;
		}
		Times.Add(F.FrameMs);
		Worst = FMath::Max(Worst, F.FrameMs);
		Over33[F.Stop] += F.FrameMs > 33.f;
		Over100[F.Stop] += F.FrameMs > 100.f;
	}
	Times.Sort();
	auto Pct = [&Times](float P) { return Times.Num() ? Times[FMath::Min(Times.Num() - 1, int32(Times.Num() * P))] : 0.f; };
	int32 Total33 = 0, Total100 = 0;
	for (int32 i = 0; i < Stops.Num(); ++i)
	{
		Total33 += Over33[i];
		Total100 += Over100[i];
		UE_LOG(LogMeridian, Display, TEXT("MRHitch: stop %-14s >33ms=%d >100ms=%d"), *Stops[i].Name, Over33[i], Over100[i]);
	}
	UE_LOG(LogMeridian, Display,
		TEXT("MRHitch: DONE label=%s ready=%.1fs frames=%d p50=%.1fms p95=%.1fms p99=%.1fms max=%.1fms >33ms=%d >100ms=%d (%s)"),
		*Label, WarmupSeconds, Times.Num(), Pct(0.5f), Pct(0.95f), Pct(0.99f), Worst, Total33, Total100, *Path);
	if (APlayerController* PC = Controller.Get())
	{
		if (!bCsv)
		{
			PC->ConsoleCommand(TEXT("quit"));
			return;
		}
		PC->ConsoleCommand(TEXT("CsvProfile Stop"));
		// the CSV is written on another thread: give it a moment before quitting
		TWeakObjectPtr<APlayerController> Weak(PC);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			if (APlayerController* P = Weak.Get())
			{
				P->ConsoleCommand(TEXT("quit"));
			}
			return false;
		}), 5.f);
	}
}
