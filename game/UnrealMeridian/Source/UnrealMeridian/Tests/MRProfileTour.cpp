#include "Tests/MRProfileTour.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteData.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "RHIStats.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	constexpr float SettleSeconds = 6.f;
	constexpr int32 SampleFrames = 180;
	constexpr float Spacing = 160.f;  // cm between characters
	constexpr float Distance = 450.f; // cm from the player to the first row
}

bool UMRProfileTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRProfile"));
}

void UMRProfileTour::Start(APlayerController* InController)
{
	Controller = InController;
	Stage = -1;
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRProfileTour::BeginStage), 8.f, false);
}

void UMRProfileTour::SpawnUpTo(int32 Count)
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Player = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Player)
	{
		return;
	}
	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const int32 PerRow = 10;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	while (Spawned.Num() < Count)
	{
		const int32 i = Spawned.Num();
		const FVector Pos = Player->GetActorLocation()
			+ Forward * (Distance + (i / PerRow) * Spacing)
			+ Right * (((i % PerRow) - (PerRow - 1) * 0.5f) * Spacing);
		FRotator FacePlayer = (-Forward).Rotation();
		FacePlayer.Yaw += (i % 8) * 45.f;  // every view angle in each row
		if (AMRCharacter* C = Player->GetWorld()->SpawnActor<AMRCharacter>(Player->GetClass(), Pos, FacePlayer, Params))
		{
			// every look in turn, random creator colours (the first few keep the look's own),
			// random heights (Phase 6); every third one dances (docs/sprites.md)
			TArray<FName> Looks;
			FMRSpriteLibrary::Get().Looks.GetKeys(Looks);
			Looks.Sort(FNameLexicalLess());
			FRandomStream Rng(77 + i);
			FMRSpriteAppearance A;
			A.Look = Looks.Num() > 0 ? Looks[i % Looks.Num()] : C->GetSpriteAppearance().Look;
			A.HeightPct = Rng.RandRange(94, 106);
			if (i > 2)
			{
				A.Skin = Rng.RandRange(0, 3);
				A.Hair = Rng.RandRange(0, 13);
				A.Shirt = Rng.RandRange(0, 10);
				A.Pants = Rng.RandRange(0, 10);
			}
			C->SetSpriteAppearance(A);
			if (i % 3 == 2)
			{
				C->PlaySpriteAction(TEXT("dance"));
			}
			Spawned.Add(C);
		}
		else
		{
			break;
		}
	}
}

void UMRProfileTour::BeginStage()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Player = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Player)
	{
		if (PC)
		{
			PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRProfileTour::BeginStage), 1.f, false);
		}
		return;
	}
	if (++Stage >= CrowdSizes.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRProfile: done (%s)"), *Player->GetAppearanceDescription());
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	// look at the crowd from third person, slightly above
	Player->SetFirstPerson(false);
	PC->SetControlRotation(FRotator(-12.f, Player->GetActorRotation().Yaw, 0.f));
	SpawnUpTo(CrowdSizes[Stage]);

	Frames = 0;
	GameMs = RenderMs = GpuMs = Draws = Prims = 0;
	PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRProfileTour::SampleFrame), SettleSeconds, false);
}

void UMRProfileTour::SampleFrame()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	Draws += GNumDrawCallsRHI[0];
	Prims += GNumPrimitivesDrawnRHI[0];
	if (++Frames < SampleFrames)
	{
		PC->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UMRProfileTour::SampleFrame));
		return;
	}
	FinishStage();
}

void UMRProfileTour::FinishStage()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Player = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	const double N = FMath::Max(Frames, 1);
	UE_LOG(LogMeridian, Display,
		TEXT("MRProfile: appearance=%s extra=%d  game=%.2fms render=%.2fms gpu=%.2fms  draws=%.0f prims=%.0f  (avg of %d frames)"),
		Player ? *Player->GetAppearanceDescription() : TEXT("?"), Spawned.Num(),
		GameMs / N, RenderMs / N, GpuMs / N, Draws / N, Prims / N, Frames);

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRProfile"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("crowd_%02d.png"), Spawned.Num())), false, false);
	if (PC)
	{
		PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRProfileTour::BeginStage), 1.f, false);
	}
}
