#include "Tests/MRSpriteClipTour.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

bool UMRSpriteClipTour::IsRequested()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("MRSpriteClip="), Value);
}

void UMRSpriteClipTour::Start(APlayerController* InController)
{
	Controller = InController;
	FParse::Value(FCommandLine::Get(), TEXT("MRSpriteClip="), Clip);
	FParse::Value(FCommandLine::Get(), TEXT("MRClipFrames="), Frames);
	// streaming, lighting and exposure settle first
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRSpriteClipTour::Setup), 6.f, false);
}

void UMRSpriteClipTour::Setup()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Character = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		if (PC)
		{
			PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRSpriteClipTour::Setup), 1.f, false);
		}
		return;
	}
	Character->SetFirstPerson(false);
	// look at the character's left side (-MRClipView=<deg>: 0 from behind, 180 from the front);
	// a walk goes to the right of the view
	float View = 90.f;
	FParse::Value(FCommandLine::Get(), TEXT("MRClipView="), View);
	CameraYaw = Character->GetActorRotation().Yaw + View;
	PC->SetControlRotation(FRotator(-6.f, CameraYaw, 0.f));
	if (Clip != TEXT("walk") && Character->GetSpriteBody())
	{
		Character->GetSpriteBody()->PlayAction(FName(*Clip));
	}
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRClip")), true);
	Index = 0;
	PC->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UMRSpriteClipTour::Frame));
}

void UMRSpriteClipTour::Frame()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Character = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		return;
	}
	if (Index >= Frames)
	{
		UE_LOG(LogMeridian, Display, TEXT("MRSpriteClip: %d frames of %s"), Frames, *Clip);
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	if (Clip == TEXT("walk"))
	{
		// across the view (the camera's right), the chase camera following
		Character->AddMovementInput(FRotator(0.f, CameraYaw + 90.f, 0.f).Vector(), 1.f);
		PC->SetControlRotation(FRotator(-6.f, CameraYaw, 0.f));
	}
	else if (Character->GetSpriteBody() && Character->GetSpriteBody()->GetAction().IsNone() && Index % 30 == 0)
	{
		Character->GetSpriteBody()->PlayAction(FName(*Clip));  // repeat one-shots
	}
	const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRClip"),
		FString::Printf(TEXT("frame_%03d.png"), Index));
	FScreenshotRequest::RequestScreenshot(File, false, false);
	++Index;
	PC->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UMRSpriteClipTour::Frame));
}
