#include "Tests/MRUIShots.h"

#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUISubsystem.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace
{
	UMRUISubsystem* UIOf(APlayerController* PC)
	{
		return PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
	}

	void SetHealth(APlayerController* PC, float Value)
	{
		AMRPlayerState* PS = PC ? PC->GetPlayerState<AMRPlayerState>() : nullptr;
		UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
		if (ASC && PS->HasAuthority())
		{
			ASC->SetNumericAttributeBase(UMRAttributeSet::GetHealthAttribute(), Value);
		}
	}
}

bool UMRUIShots::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRUIShots"));
}

void UMRUIShots::PointAt(APlayerController* PC, int32 Area, int32 Index)
{
	UMRUISubsystem* UI = UIOf(PC);
	UGameViewportClient* VC = PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
	TSharedPtr<SViewport> Viewport = VC ? VC->GetGameViewportWidget() : nullptr;
	FVector2f Abs;
	if (UI && Viewport && UI->GetSlotCentre(FMRSlotRef(static_cast<EMRSlotArea>(Area), Index), Abs))
	{
		const FVector2f Pixel = Abs - FVector2f(Viewport->GetCachedGeometry().GetAbsolutePosition());
		PC->SetMouseLocation(FMath::RoundToInt(Pixel.X), FMath::RoundToInt(Pixel.Y));
	}
}

void UMRUIShots::Start(APlayerController* InController)
{
	Controller = InController;
	FString Label = TEXT("latest");
	FParse::Value(FCommandLine::Get(), TEXT("MRUIShotsLabel="), Label);
	Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRUI"), Label);
	IFileManager::Get().MakeDirectory(*Dir, true);

	const int32 Bag = static_cast<int32>(EMRSlotArea::Bag);
	Steps = {
		{TEXT("hud"), [](APlayerController* PC) {}, 1.f},
		{TEXT("hud_select"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->OnHotbarKey(3); }, 0.4f},
		{TEXT("hud_cast"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->OnSpellKey(1); }, 0.4f},
		{TEXT("hud_low_health"), [](APlayerController* PC) { SetHealth(PC, 6.f); }, 1.5f},
		{TEXT(""), [](APlayerController* PC) { SetHealth(PC, 20.f); }, 0.5f},
		{TEXT("inventory"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetInventoryOpen(true); }, 1.5f},
		{TEXT("inventory_tooltip"), [Bag](APlayerController* PC) { PointAt(PC, Bag, 9); }, 2.f},
		{TEXT("inventory_carry"), [Bag](APlayerController* PC)
			{
				if (UMRUISubsystem* UI = UIOf(PC))
				{
					UI->GetSource()->Click(FMRSlotRef(EMRSlotArea::Bag, 9), false);
					PointAt(PC, Bag, 31);
				}
			}, 0.8f},
		{TEXT(""), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->GetSource()->ReturnCursor(); PC->SetMouseLocation(5, 5); }, 0.3f},
		{TEXT("inventory_avatar_turned"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->TurnAvatar(3); }, 0.8f},
		{TEXT("tab_spells"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetInventoryTab(1); }, 0.8f},
		{TEXT("tab_skills"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetInventoryTab(2); }, 0.8f},
		{TEXT("tab_stats"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetInventoryTab(3); }, 0.8f},
		{TEXT("tab_quests"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetInventoryTab(4); }, 0.8f},
		{TEXT("hud_closed_again"), [](APlayerController* PC)
			{
				if (UMRUISubsystem* UI = UIOf(PC))
				{
					UI->SetInventoryTab(0);
					UI->SetInventoryOpen(false);
				}
			}, 1.f},
	};
	Index = -1;
	// streaming, lighting and the HUD settle first
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRUIShots::Next), 8.f, false);
}

void UMRUIShots::Next()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	if (!PC->GetPawn() || !UIOf(PC) || !UIOf(PC)->HasHUD())
	{
		PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRUIShots::Next), 1.f, false);
		return;
	}
	if (++Index >= Steps.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRUIShots: done"));
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FStep& Step = Steps[Index];
	Step.Do(PC);
	const FString Name = Step.Name;
	PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, Name]()
	{
		if (!Name.IsEmpty())
		{
			const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("%02d_%s.png"), Index, *Name));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogMeridian, Display, TEXT("MRUIShots: %s"), *File);
		}
		if (APlayerController* P = Controller.Get())
		{
			P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRUIShots::Next), 0.5f, false);
		}
	}), Step.Delay, false);
}
