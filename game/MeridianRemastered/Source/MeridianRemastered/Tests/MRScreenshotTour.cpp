#include "Tests/MRScreenshotTour.h"

#include "Animation/AnimMontage.h"
#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

bool UMRScreenshotTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRScreenshots"));
}

void UMRScreenshotTour::Start(APlayerController* InController)
{
	Controller = InController;
	Shots = {
		{TEXT("third_person"),       false, -12.f, 160.f},
		{TEXT("third_person_side"),  false, -8.f,  90.f},
		{TEXT("third_person_above"), false, -70.f, 0.f},
		{TEXT("first_person"),       true,  0.f,   0.f},
		{TEXT("first_person_down30"), true, -30.f, 0.f},
		{TEXT("first_person_down50"), true, -50.f, 0.f},
		{TEXT("first_person_down"),  true,  -70.f, 0.f},
		{TEXT("attack_side"),        false, -8.f,  90.f, TEXT("/Game/Characters/Animations/Quaternius/AM_Sword_Regular_A.AM_Sword_Regular_A"), 0.2f},
		{TEXT("attack_front"),       false, -12.f, 160.f, TEXT("/Game/Characters/Animations/Quaternius/AM_Sword_Attack.AM_Sword_Attack"), 0.55f},
		{TEXT("attack_first_person"), true, -10.f, 0.f, TEXT("/Game/Characters/Animations/Quaternius/AM_Sword_Regular_B.AM_Sword_Regular_B"), 0.2f},
		{TEXT("view_behind"),        false, 0.f,   0.f, FString(), 1.5f, static_cast<int32>(EMRViewMode::Behind)},
		{TEXT("view_front"),         false, 0.f,   0.f, FString(), 1.5f, static_cast<int32>(EMRViewMode::Front)},
	};
	Index = -1;
	// give streaming, lighting and the animation a few seconds to settle
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRScreenshotTour::Next), 6.f, false);
}

void UMRScreenshotTour::Next()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	AMRCharacter* Character = Cast<AMRCharacter>(PC->GetPawn());
	if (!Character)
	{
		PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRScreenshotTour::Next), 1.f, false);
		return;
	}
	if (!bSpriteShotsAdded && Character->UsesSpriteBody())
	{
		bSpriteShotsAdded = true;
		for (int32 Deg = 0; Deg < 360; Deg += 45)
		{
			Shots.Add({FString::Printf(TEXT("sprite_angle_%03d"), Deg), false, -10.f, 180.f + Deg, FString(), 1.f});
		}
	}
	if (++Index >= Shots.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRScreenshots: done (%s)"), *Character->GetAppearanceDescription());
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FShot& Shot = Shots[Index];
	if (Shot.View >= 0)
	{
		Character->SetViewMode(static_cast<EMRViewMode>(Shot.View));
	}
	else
	{
		Character->SetFirstPerson(Shot.bFirstPerson);
	}
	// third-person shots look back at the character from in front / the side
	const float Yaw = Character->GetActorRotation().Yaw + Shot.YawOffset;
	PC->SetControlRotation(FRotator(Shot.Pitch, Yaw, 0.f));

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRTour"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = FPaths::Combine(Dir, Shot.Name + TEXT(".png"));
	float Delay = Shot.CaptureDelay;
	if (!Shot.Montage.IsEmpty() && Character->GetSpriteBody())
	{
		// the sprite's own attack instead of the montage: capture its second pose
		UMRSpriteBodyComponent* Sprite = Character->GetSpriteBody();
		const FMRSpriteLook* Look = FMRSpriteLibrary::Get().Looks.Find(Sprite->GetLook());
		Sprite->PlayAction(Look && Look->Find(TEXT("weapon")) ? TEXT("weapon_attack") : TEXT("fist_attack"));
		Delay = 0.35f;
	}
	else if (!Shot.Montage.IsEmpty())
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Shot.Montage);
		const float Length = Montage ? Character->PlayAnimMontage(Montage) : 0.f;
		UE_LOG(LogMeridian, Display, TEXT("MRScreenshots: montage %s -> %s"), *Shot.Montage,
			Length > 0.f ? *FString::Printf(TEXT("playing %.2fs"), Length) : TEXT("NOT PLAYING"));
		Delay = FMath::Max(Delay, 0.05f);
	}
	// wait a moment for the camera to settle, then capture, then move on
	PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, File]()
	{
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogMeridian, Display, TEXT("MRScreenshots: %s"), *File);
		if (APlayerController* P = Controller.Get())
		{
			P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRScreenshotTour::Next), 1.f, false);
		}
	}), Delay, false);
}
