#include "Tests/MRUIShots.h"
#include "Net/MRNetWorld.h"

#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Net/MRNetSubsystem.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRCharCreator.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace
{
	UMRUISubsystem* UIOf(APlayerController* PC)
	{
		return PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
	}

	UMRNetSubsystem* NetOf(APlayerController* PC)
	{
		const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	}

	/** The login screen's pages without a server (UMRNetSubsystem::SetPreview). */
	void PreviewLogin(APlayerController* PC, EMRNetPhase Phase, bool bCharacters = false, const FString& Error = FString())
	{
		UMRNetSubsystem* Net = NetOf(PC);
		UMRUISubsystem* UI = UIOf(PC);
		if (!Net || !UI)
		{
			return;
		}
		TArray<FMRCharacterSlot> Slots;
		if (bCharacters)
		{
			Slots.Add({1, TEXT("Tomas d'Raza"), false});
			Slots.Add({2, TEXT("Aldera"), false});
			Slots.Add({3, FString(), true});
			Slots.Add({4, FString(), true});
		}
		Net->SetPreview(Phase, Slots, Error);
		UI->ShowLogin(PC);
	}

	/** The character creator without a server: the options from data/charinfo.json. */
	void PreviewCreator(APlayerController* PC)
	{
		UMRNetSubsystem* Net = NetOf(PC);
		UMRUISubsystem* UI = UIOf(PC);
		FMRCharInfo Info;
		if (!Net || !UI || !UMRNetSubsystem::LoadMockCharInfo(Info))
		{
			return;
		}
		Net->SetPreview(EMRNetPhase::Characters, {{3, FString(), true}});
		UI->ShowLogin(PC);
		Net->SetPreviewCharInfo(Info, 3);
		if (TSharedPtr<SMRCharCreator> C = UI->GetCreator())
		{
			C->Begin(7);  // the same "random" start every run
			C->SetNameAndDescription(TEXT("Aldera"), TEXT("A traveller from Marion, new to Raza."));
		}
	}

	void WithCreator(APlayerController* PC, TFunction<void(SMRCharCreator&)> Do)
	{
		UMRUISubsystem* UI = UIOf(PC);
		if (TSharedPtr<SMRCharCreator> C = UI ? UI->GetCreator() : nullptr)
		{
			Do(*C);
		}
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
		{TEXT("tab_spells_folded"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) { UI->DebugFoldSpellSchool(1); UI->DebugFoldSpellSchool(3); } }, 0.8f},
		{TEXT("tab_spells_search"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->DebugSearch(1, TEXT("to")); }, 0.8f},
		{TEXT(""), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) { UI->DebugSearch(1, FString()); UI->DebugFoldSpellSchool(1); UI->DebugFoldSpellSchool(3); } }, 0.3f},
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
		// the Escape menu (offline: no Log Off)
		{TEXT("game_menu"), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetGameMenuOpen(true); }, 0.8f},
		{TEXT(""), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->SetGameMenuOpen(false); }, 0.3f},
		// retraining (a town elder's BP_STAT_CHANGE): the dialog with an offer, as the server would send it
		{TEXT("stat_change"), [](APlayerController* PC)
			{
				if (UMRUISubsystem* UI = UIOf(PC))
				{
					FMRNetStatChange Offer;
					const uint8 Stats[6] = {45, 20, 40, 35, 25, 35};
					FMemory::Memcpy(Offer.Stats, Stats, sizeof(Stats));
					UI->DebugShowStatChange(Offer);
				}
			}, 0.8f},
		{TEXT(""), [](APlayerController* PC) { if (UMRUISubsystem* UI = UIOf(PC)) UI->CloseStatChange(); }, 0.3f},
		// the login screen (docs/adr/0010-meridian-servers.md), over the HUD here; online it has the town to itself
		{TEXT("login"), [](APlayerController* PC) { PreviewLogin(PC, EMRNetPhase::Offline); }, 1.f},
		{TEXT("login_error"), [](APlayerController* PC) { PreviewLogin(PC, EMRNetPhase::Offline, false, TEXT("Login failed. Check your password.")); }, 0.6f},
		{TEXT("login_connecting"), [](APlayerController* PC) { PreviewLogin(PC, EMRNetPhase::Connecting); }, 0.6f},
		{TEXT("login_characters"), [](APlayerController* PC) { PreviewLogin(PC, EMRNetPhase::Characters, true); }, 0.6f},
		// the character creator, with the options the server would send (data/charinfo.json)
		{TEXT(""), [](APlayerController* PC) { PreviewCreator(PC); }, 1.0f},
		// typing into the description the way a player does: focus, then key characters through Slate
		{TEXT("creator_name"), [](APlayerController* PC)
			{
				WithCreator(PC, [](SMRCharCreator& C)
				{
					C.SetNameAndDescription(TEXT("Aldera"), FString());
					C.FocusDescription();
					for (const TCHAR Ch : FString(TEXT("Typed: a traveller from Marion.")))
					{
						FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false));
					}
					UE_LOG(LogMeridian, Display, TEXT("MRUIShots: description after typing: \"%s\""), *C.GetDescriptionText());
				});
			}, 1.0f},
		{TEXT("creator_appearance"), [](APlayerController* PC) { WithCreator(PC, [](SMRCharCreator& C) { C.SetTab(SMRCharCreator::ETab::Appearance); }); }, 2.f},
		{TEXT("creator_appearance_male"), [](APlayerController* PC)
			{
				WithCreator(PC, [](SMRCharCreator& C)
				{
					FMRNewCharacter& S = C.GetCharacter();
					S.bFemale = !S.bFemale;
					S.Hair += 2;
					S.Eyes += 1;
					S.HairColour += 5;
					S.Skin = 3;
					C.Refresh();
				});
				if (UMRUISubsystem* UI = UIOf(PC))
				{
					UI->TurnCreatorPortrait(1);  // the head three-quarters on
				}
			}, 2.f},
		{TEXT("creator_stats"), [](APlayerController* PC) { WithCreator(PC, [](SMRCharCreator& C) { C.SetTab(SMRCharCreator::ETab::Stats); C.ApplyPreset(0); }); }, 0.8f},
		{TEXT("creator_spells"), [](APlayerController* PC)
			{
				WithCreator(PC, [](SMRCharCreator& C) { C.SetTab(SMRCharCreator::ETab::Spells); C.AddAbility(false, 0); C.AddAbility(false, 6); C.AddAbility(false, 9); });
			}, 0.8f},
		{TEXT("creator_skills"), [](APlayerController* PC) { WithCreator(PC, [](SMRCharCreator& C) { C.SetTab(SMRCharCreator::ETab::Skills); C.AddAbility(true, 1); }); }, 0.8f},
		{TEXT("creator_error"), [](APlayerController* PC) { WithCreator(PC, [](SMRCharCreator& C) { C.SetNameAndDescription(TEXT("Al"), FString()); C.PressOk(); }); }, 0.8f},
		{TEXT("creator_error_points"), [](APlayerController* PC)
			{
				WithCreator(PC, [](SMRCharCreator& C) { C.SetNameAndDescription(TEXT("Aldera"), FString()); C.GetCharacter().Stats[0] -= 3; C.PressOk(); });
			}, 0.8f},
		{TEXT(""), [](APlayerController* PC)
			{
				if (UMRUISubsystem* UI = UIOf(PC))
				{
					UI->HideLogin();
				}
				if (UMRNetSubsystem* Net = NetOf(PC))
				{
					Net->SetPreview(EMRNetPhase::Offline, {});
				}
			}, 0.3f},
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
