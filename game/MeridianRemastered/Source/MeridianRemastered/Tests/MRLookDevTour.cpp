#include "Tests/MRLookDevTour.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderTimer.h"
#include "RHIStats.h"
#include "DynamicRHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	/** After a camera cut, Lumen, virtual shadow maps and auto exposure need time to settle. The frame
	 *  is sampled every SampleSeconds from MinSettleSeconds on; it has settled when a GridX x GridY
	 *  brightness grid differs from the sample CompareBack samples earlier by less than SettledDiff
	 *  (0..255, mean over cells) StableSamples times in a row. The coarse grid ignores grass blowing in
	 *  the wind but sees exposure, GI and shadows still moving. MaxSettleSeconds caps it. */
	constexpr float MinSettleSeconds = 0.3f;
	constexpr float MaxSettleSeconds = 8.f;
	constexpr float SampleSeconds = 0.1f;
	constexpr int32 CompareBack = 2;
	constexpr int32 StableSamples = 2;
	constexpr float SettledDiff = 0.25f;
	constexpr int32 GridX = 48, GridY = 27;

	/** World time runs this much slower while capturing: clouds, grass wind and water hold still, so
	 *  a camera looks the same however long it took to settle (and from run to run), while Lumen,
	 *  shadows and TSR, which converge per frame, are unaffected. Auto exposure adapts on world time,
	 *  so it holds still too, except on a camera cut, where the engine sets it straight to the target
	 *  for the current frame: each camera is cut to, settled, cut to again (exposure for the settled
	 *  lighting) and settled again before it is saved. */
	constexpr float CaptureTimeDilation = 0.001f;

	TArray<float> BrightnessGrid(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		TArray<float> Sum;
		TArray<int32> Count;
		Sum.SetNumZeroed(GridX * GridY);
		Count.SetNumZeroed(GridX * GridY);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const int32 Row = FMath::Min(Y * GridY / Height, GridY - 1) * GridX;
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor& C = Pixels[Y * Width + X];
				const int32 Cell = Row + FMath::Min(X * GridX / Width, GridX - 1);
				Sum[Cell] += (C.R + C.G + C.B) / 3.f;
				++Count[Cell];
			}
		}
		for (int32 i = 0; i < Sum.Num(); ++i)
		{
			Sum[i] /= FMath::Max(Count[i], 1);
		}
		return Sum;
	}

	float MeanDiff(const TArray<float>& A, const TArray<float>& B)
	{
		double D = 0;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			D += FMath::Abs(A[i] - B[i]);
		}
		return A.Num() ? D / A.Num() : 0.f;
	}

	/** Profile mode: settle time after switching a variant, and frames averaged per variant. */
	constexpr float VariantSettleSeconds = 2.5f;
	constexpr int32 SampleFrames = 150;
}

bool UMRLookDevTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRLookDev"));
}

FString UMRLookDevTour::FormatBookmark(const FString& Name, const FVector& Location, const FRotator& Rotation, float Fov)
{
	return FString::Printf(TEXT("{\"name\": \"%s\", \"location_cm\": [%.0f, %.0f, %.0f], \"rotation\": [%.1f, %.1f], \"fov\": %.0f}"),
		*Name, Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Fov);
}

bool UMRLookDevTour::LoadShots()
{
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("environment"), TEXT("lookdev_cameras.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogMeridian, Error, TEXT("MRLookDev: could not read %s"), *Path);
		return false;
	}

	FString OnlyList;
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevOnly="), OnlyList, /*bShouldStopOnSeparator*/ false);
	TArray<FString> Only;
	OnlyList.ParseIntoArray(Only, TEXT(","));

	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cameras")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& L = O->GetArrayField(TEXT("location_cm"));
		const TArray<TSharedPtr<FJsonValue>>& R = O->GetArrayField(TEXT("rotation"));
		if (L.Num() != 3 || R.Num() != 2)
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRLookDev: skipping malformed camera %s"), *O->GetStringField(TEXT("name")));
			continue;
		}
		FShot Shot;
		Shot.Name = O->GetStringField(TEXT("name"));
		if (Only.Num() && !Only.Contains(Shot.Name))
		{
			continue;
		}
		Shot.Location = FVector(L[0]->AsNumber(), L[1]->AsNumber(), L[2]->AsNumber());
		Shot.Rotation = FRotator(R[0]->AsNumber(), R[1]->AsNumber(), 0.f);
		double Fov = 90.0;
		O->TryGetNumberField(TEXT("fov"), Fov);
		Shot.Fov = Fov;
		Shots.Add(Shot);
	}
	return Shots.Num() > 0;
}

void UMRLookDevTour::Start(APlayerController* InController)
{
	Controller = InController;
	FString Label = TEXT("latest");
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevLabel="), Label);
	OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRLookDev"), Label);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	if (!LoadShots())
	{
		InController->ConsoleCommand(TEXT("quit"));
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevSettle="), FixedSettle);
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevAudio="), AudioSeconds);
	if (AudioSeconds > 0.f && FixedSettle <= 0.f)
	{
		FixedSettle = 2.f;  // no slowed world clock while recording sound
	}
	bProfile = FParse::Param(FCommandLine::Get(), TEXT("MRLookDevProfile"));
	if (bProfile)
	{
		// uncapped, with per-pass GPU timings in the CSV captures
		for (const TCHAR* Cmd : {TEXT("r.VSync 0"), TEXT("t.MaxFPS 0"), TEXT("r.GPUCsvStatsEnabled 1")})
		{
			InController->ConsoleCommand(Cmd);
		}
		Variants = {
			{TEXT("full"), {TEXT("r.Nanite.Tessellation 1"), TEXT("ShowFlag.InstancedStaticMeshes 1"), TEXT("mr.Fire.CullDistanceM 60")}},
			{TEXT("no_tess"), {TEXT("r.Nanite.Tessellation 0"), TEXT("ShowFlag.InstancedStaticMeshes 1"), TEXT("mr.Fire.CullDistanceM 60")}},
			{TEXT("no_grass"), {TEXT("r.Nanite.Tessellation 1"), TEXT("ShowFlag.InstancedStaticMeshes 0"), TEXT("mr.Fire.CullDistanceM 60")}},
			// fire lights off (UMRFireSubsystem): what the fires' lights and shadows cost
			{TEXT("no_fire"), {TEXT("r.Nanite.Tessellation 1"), TEXT("ShowFlag.InstancedStaticMeshes 1"), TEXT("mr.Fire.CullDistanceM 0")}},
		};
	}
	Index = -1;
	// give zone streaming a moment; the first camera's settle check covers the rest
	After(FixedSettle > 0.f ? 6.f : 2.f, &UMRLookDevTour::Next);
}

void UMRLookDevTour::After(float Seconds, void (UMRLookDevTour::*Step)())
{
	// real time (FTSTicker), not world timers: world time is slowed down while capturing
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Step](float)
	{
		(this->*Step)();
		return false;
	}), Seconds);
}

void UMRLookDevTour::Next()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	if (!Camera)
	{
		Camera = PC->GetWorld()->SpawnActor<ACameraActor>();
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (FixedSettle <= 0.f)
		{
			// from the first camera on, not from BeginPlay: the game's first frames render at normal speed
			UGameplayStatics::SetGlobalTimeDilation(PC, CaptureTimeDilation);
		}
	}
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorHiddenInGame(true);
	}
	if (++Index >= Shots.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRLookDev: done, %d shots in %s"), Shots.Num(), *OutDir);
		UGameViewportClient::OnScreenshotCaptured().Remove(FrameHandle);
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FShot& Shot = Shots[Index];
	Camera->SetActorLocationAndRotation(Shot.Location, Shot.Rotation);
	Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	PC->SetViewTarget(Camera);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->SetGameCameraCutThisFrame();  // drop history from the last camera
	}

	ShotStart = FPlatformTime::Seconds();
	bExposureCut = false;
	Grids.Reset();
	StableCount = 0;
	After(FixedSettle > 0.f ? FixedSettle : MinSettleSeconds, &UMRLookDevTour::Sample);
}

void UMRLookDevTour::Sample()
{
	if (!FrameHandle.IsValid())
	{
		// while bound, screenshots come here as pixels instead of going to disk
		FrameHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UMRLookDevTour::OnFrame);
	}
	FScreenshotRequest::RequestScreenshot(false, false);
}

void UMRLookDevTour::OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	APlayerController* PC = Controller.Get();
	if (!PC || !Shots.IsValidIndex(Index) || Pixels.Num() != Width * Height)
	{
		return;
	}
	const FShot& Shot = Shots[Index];
	const double Elapsed = FPlatformTime::Seconds() - ShotStart;
	Grids.Add(BrightnessGrid(Width, Height, Pixels));
	const float Diff = Grids.Num() > CompareBack ? MeanDiff(Grids.Last(), Grids[Grids.Num() - 1 - CompareBack]) : -1.f;
	StableCount = Diff >= 0.f && Diff < SettledDiff ? StableCount + 1 : 0;
	UE_LOG(LogMeridian, Log, TEXT("MRLookDev: %s t=%.2fs diff=%.2f"), *Shot.Name, Elapsed, Diff);

	const bool bSettled = StableCount >= StableSamples;
	if (bSettled && !bExposureCut && FixedSettle <= 0.f)
	{
		// lighting has settled under the exposure picked at the first cut; cut again so exposure is
		// picked from this frame, then wait for the (shorter) second settle
		bExposureCut = true;
		Grids.Reset();
		StableCount = 0;
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
		After(MinSettleSeconds, &UMRLookDevTour::Sample);
		return;
	}
	const bool bDone = FixedSettle > 0.f || bSettled || Elapsed >= MaxSettleSeconds;
	if (!bDone)
	{
		After(SampleSeconds, &UMRLookDevTour::Sample);
		return;
	}
	const FString File = FPaths::Combine(OutDir, Shot.Name + TEXT(".png"));
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	if (!FFileHelper::SaveArrayToFile(Png, *File))
	{
		UE_LOG(LogMeridian, Error, TEXT("MRLookDev: could not write %s"), *File);
	}
	UE_LOG(LogMeridian, Display, TEXT("MRLookDev: %s (%s after %.1f s)"), *File,
		FixedSettle > 0.f ? TEXT("fixed wait") : StableCount >= StableSamples ? TEXT("settled") : TEXT("NOT settled, gave up"), Elapsed);
	After(0.f, &UMRLookDevTour::Captured);
}

void UMRLookDevTour::Captured()
{
	if (APlayerController* P = Controller.Get())
	{
		VariantIndex = -1;
		if (AudioSeconds > 0.f)
		{
			UAudioMixerBlueprintLibrary::StartRecordingOutput(P, AudioSeconds);
			After(AudioSeconds, &UMRLookDevTour::StopRecording);
			return;
		}
		After(0.1f, bProfile ? &UMRLookDevTour::NextVariant : &UMRLookDevTour::Next);
	}
}

void UMRLookDevTour::StopRecording()
{
	if (APlayerController* P = Controller.Get(); P && Shots.IsValidIndex(Index))
	{
		// the main mix as heard at the camera: <label>/<shot>.wav
		UAudioMixerBlueprintLibrary::StopRecordingOutput(P, EAudioRecordingExportType::WavFile, Shots[Index].Name, OutDir);
		UE_LOG(LogMeridian, Display, TEXT("MRLookDevAudio: %s.wav (%.0f s)"), *FPaths::Combine(OutDir, Shots[Index].Name), AudioSeconds);
	}
	After(0.2f, &UMRLookDevTour::Next);
}

void UMRLookDevTour::NextVariant()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	if (++VariantIndex >= Variants.Num())
	{
		for (const FString& Cmd : Variants[0].Commands)
		{
			PC->ConsoleCommand(Cmd);  // back to everything on for the next camera's screenshot
		}
		After(0.2f, &UMRLookDevTour::Next);
		return;
	}
	for (const FString& Cmd : Variants[VariantIndex].Commands)
	{
		PC->ConsoleCommand(Cmd);
	}
	After(VariantSettleSeconds, &UMRLookDevTour::StartSampling);
}

void UMRLookDevTour::StartSampling()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	Frames = 0;
	GameMs = RenderMs = GpuMs = 0;
#if CSV_PROFILER
	FCsvProfiler::Get()->BeginCapture(SampleFrames, OutDir,
		FString::Printf(TEXT("%s__%s.csv"), *Shots[Index].Name, *Variants[VariantIndex].Name));
#endif
	After(0.f, &UMRLookDevTour::SampleFrame);
}

void UMRLookDevTour::SampleFrame()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	if (++Frames < SampleFrames)
	{
		After(0.f, &UMRLookDevTour::SampleFrame);
		return;
	}
	const double N = FMath::Max(Frames, 1);
	UE_LOG(LogMeridian, Display, TEXT("MRLookDevProfile: camera=%s variant=%s game=%.2fms render=%.2fms gpu=%.2fms (avg of %d frames)"),
		*Shots[Index].Name, *Variants[VariantIndex].Name, GameMs / N, RenderMs / N, GpuMs / N, Frames);
	// the capture ends itself after SampleFrames; give it a moment to write
	After(0.5f, &UMRLookDevTour::NextVariant);
}
