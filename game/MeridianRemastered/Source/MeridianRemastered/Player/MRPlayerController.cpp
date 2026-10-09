#include "Player/MRPlayerController.h"
#include "Core/MRSettings.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/MRCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRLookDialog.h"
#include "MeridianRemastered.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Player/MRPlayerState.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRMonsterTour.h"
#include "Tests/MRMapCapture.h"
#include "Tests/MRMoveTest.h"
#include "Tests/MRNetTest.h"
#include "Tests/MRUIShots.h"
#include "Zones/MRZoneSubsystem.h"

AMRPlayerController::AMRPlayerController()
{
}

void AMRPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			BuildUIInput();
			Input->AddMappingContext(UIContext, 1);
		}
		if (UMRScreenshotTour::IsRequested())
		{
			ScreenshotTour = NewObject<UMRScreenshotTour>(this);
			ScreenshotTour->Start(this);
		}
		else if (UMRProfileTour::IsRequested())
		{
			ProfileTour = NewObject<UMRProfileTour>(this);
			ProfileTour->Start(this);
		}
		else if (UMRMonsterTour::IsRequested())
		{
			MonsterTour = NewObject<UMRMonsterTour>(this);
			MonsterTour->Start(this);
		}
		else if (UMRSpriteClipTour::IsRequested())
		{
			SpriteClipTour = NewObject<UMRSpriteClipTour>(this);
			SpriteClipTour->Start(this);
		}
		else if (UMRMapCapture::IsRequested())
		{
			MapCapture = NewObject<UMRMapCapture>(this);
			MapCapture->Start(this);
		}
		else if (UMRUIShots::IsRequested())
		{
			UIShots = NewObject<UMRUIShots>(this);
			UIShots->Start(this);
		}
		else if (UMRLookDevTour::IsRequested())
		{
			LookDevTour = NewObject<UMRLookDevTour>(this);
			LookDevTour->Start(this);
		}
		else if (UMRNetTest::IsRequested())
		{
			NetTest = NewObject<UMRNetTest>(this);
			NetTest->Start(this);
		}
		else if (UMRMoveTest::IsRequested())
		{
			MoveTest = NewObject<UMRMoveTest>(this);
			MoveTest->Start(this);
		}
	}
}

// ---------------------------------------------------------------------------- UI input

void AMRPlayerController::BuildUIInput()
{
	if (UIContext)
	{
		return;
	}
	auto MakeAction = [this](const FString& Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, FName(*Name));
		A->ValueType = Type;
		return A;
	};
	UIContext = NewObject<UInputMappingContext>(this, TEXT("IMC_UI"));
	for (int32 i = 0; i < 9; ++i)
	{
		HotbarActions.Add(MakeAction(FString::Printf(TEXT("IA_Hotbar%d"), i + 1), EInputActionValueType::Boolean));
		SpellActions.Add(MakeAction(FString::Printf(TEXT("IA_Spell%d"), i + 1), EInputActionValueType::Boolean));
	}
	for (int32 i = 0; i < 12; ++i)
	{
		QuickChatActions.Add(MakeAction(FString::Printf(TEXT("IA_QuickChat%d"), i + 1), EInputActionValueType::Boolean));
	}
	HotbarScrollAction = MakeAction(TEXT("IA_HotbarScroll"), EInputActionValueType::Axis1D);
	InventoryAction = MakeAction(TEXT("IA_Inventory"), EInputActionValueType::Boolean);
	ChatAction = MakeAction(TEXT("IA_Chat"), EInputActionValueType::Boolean);
	MapZoomAction = MakeAction(TEXT("IA_MapZoom"), EInputActionValueType::Axis1D);
	MenuAction = MakeAction(TEXT("IA_Menu"), EInputActionValueType::Boolean);
	TargetNextAction = MakeAction(TEXT("IA_TargetNext"), EInputActionValueType::Boolean);
	TargetPreviousAction = MakeAction(TEXT("IA_TargetPrevious"), EInputActionValueType::Boolean);
	TargetSelfAction = MakeAction(TEXT("IA_TargetSelf"), EInputActionValueType::Boolean);
	TargetAimAction = MakeAction(TEXT("IA_TargetAim"), EInputActionValueType::Boolean);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Boolean);
	GetAction = MakeAction(TEXT("IA_Get"), EInputActionValueType::Boolean);
	UseAction = MakeAction(TEXT("IA_Use"), EInputActionValueType::Boolean);
	RestAction = MakeAction(TEXT("IA_Rest"), EInputActionValueType::Boolean);
	ApplyAction = MakeAction(TEXT("IA_Apply"), EInputActionValueType::Boolean);
	WhoAction = MakeAction(TEXT("IA_Who"), EInputActionValueType::Boolean);
	MailAction = MakeAction(TEXT("IA_Mail"), EInputActionValueType::Boolean);
	GuildAction = MakeAction(TEXT("IA_Guild"), EInputActionValueType::Boolean);
	MapWindowAction = MakeAction(TEXT("IA_MapWindow"), EInputActionValueType::Boolean);
	MapUIKeys();
}

void AMRPlayerController::MapUIKeys()
{
	// the player's keys (MRKeys: Options > Controls); the second keys of some stay as they were
	UInputMappingContext* Ctx = UIContext;
	Ctx->UnmapAll();
	MappedKeyVersion = MRKeys::Version();
	auto Map = [Ctx](UInputAction* Action, FName Binding) -> FEnhancedActionKeyMapping*
	{
		const FKey Key = MRKeys::Get(Binding);
		return Key.IsValid() ? &Ctx->MapKey(Action, Key) : nullptr;
	};
	const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	const FKey NumPad[] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
		EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
	for (int32 i = 0; i < 9; ++i)
	{
		Ctx->MapKey(HotbarActions[i], Numbers[i]);
		Ctx->MapKey(SpellActions[i], NumPad[i]);
	}
	// the quick chat (the original's F-key aliases; F10 stays the menu's)
	const FKey FKeys[] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9,
		EKeys::Invalid, EKeys::F11, EKeys::F12};
	for (int32 i = 0; i < 12; ++i)
	{
		if (FKeys[i].IsValid())
		{
			Ctx->MapKey(QuickChatActions[i], FKeys[i]);
		}
	}
	Ctx->MapKey(HotbarScrollAction, EKeys::MouseWheelAxis);
	Map(InventoryAction, TEXT("Inventory"));
	Ctx->MapKey(InventoryAction, MRKeys::Get(TEXT("Inventory")) == EKeys::I ? EKeys::E : EKeys::I);
	Map(ChatAction, TEXT("Chat"));
	Map(MapZoomAction, TEXT("MapZoomIn"));
	Ctx->MapKey(MapZoomAction, EKeys::Add);
	if (FEnhancedActionKeyMapping* Out = Map(MapZoomAction, TEXT("MapZoomOut")))
	{
		Out->Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
	}
	Ctx->MapKey(MapZoomAction, EKeys::Subtract).Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
	Ctx->MapKey(MenuAction, EKeys::Escape);
	Ctx->MapKey(MenuAction, EKeys::F10);
	Map(TargetNextAction, TEXT("TargetNext"));
	Ctx->MapKey(TargetNextAction, EKeys::RightBracket);
	Map(TargetPreviousAction, TEXT("TargetPrevious"));
	Map(TargetSelfAction, TEXT("TargetSelf"));
	Map(TargetAimAction, TEXT("TargetAim"));
	Map(LookAction, TEXT("Look"));
	Map(GetAction, TEXT("Get"));
	Map(UseAction, TEXT("Use"));
	Map(RestAction, TEXT("Rest"));
	Map(ApplyAction, TEXT("Apply"));
	Map(WhoAction, TEXT("Who"));
	Map(MailAction, TEXT("Mail"));
	Map(GuildAction, TEXT("Guild"));
	Map(MapWindowAction, TEXT("Map"));
}

void AMRPlayerController::RemapKeysIfChanged()
{
	if (UIContext && MappedKeyVersion != MRKeys::Version())
	{
		MapUIKeys();
	}
	if (AMRCharacter* Char = Cast<AMRCharacter>(GetPawn()))
	{
		Char->RemapKeysIfChanged();
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->RequestRebuildControlMappings();
	}
}

void AMRPlayerController::OnQuickChatKey(int32 Index)
{
	// online: the key's line, run or put in the chat line (Options > Chat; the original's alias.c)
	const UMRNetWorldSubsystem* Net = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	if (UMRUISubsystem* UI = GetUI(); UI && Net && Net->IsActive())
	{
		UI->RunQuickChat(Index);
	}
}

void AMRPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	BuildUIInput();
	for (int32 i = 0; i < HotbarActions.Num(); ++i)
	{
		Input->BindAction(HotbarActions[i], ETriggerEvent::Started, this, &AMRPlayerController::OnHotbarKey, i);
		Input->BindAction(SpellActions[i], ETriggerEvent::Started, this, &AMRPlayerController::OnSpellKey, i);
	}
	Input->BindAction(HotbarScrollAction, ETriggerEvent::Triggered, this, &AMRPlayerController::OnHotbarScroll);
	Input->BindAction(InventoryAction, ETriggerEvent::Started, this, &AMRPlayerController::OnInventoryKey);
	Input->BindAction(ChatAction, ETriggerEvent::Started, this, &AMRPlayerController::OnChatKey);
	Input->BindAction(MapZoomAction, ETriggerEvent::Started, this, &AMRPlayerController::OnMapZoom);
	Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AMRPlayerController::OnMenuKey);
	Input->BindAction(TargetNextAction, ETriggerEvent::Started, this, &AMRPlayerController::OnTargetNext);
	Input->BindAction(TargetPreviousAction, ETriggerEvent::Started, this, &AMRPlayerController::OnTargetPrevious);
	Input->BindAction(TargetSelfAction, ETriggerEvent::Started, this, &AMRPlayerController::OnTargetSelf);
	Input->BindAction(TargetAimAction, ETriggerEvent::Started, this, &AMRPlayerController::OnTargetAim);
	Input->BindAction(LookAction, ETriggerEvent::Started, this, &AMRPlayerController::OnLookKey);
	Input->BindAction(GetAction, ETriggerEvent::Started, this, &AMRPlayerController::OnGetKey);
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AMRPlayerController::OnUseKey);
	Input->BindAction(RestAction, ETriggerEvent::Started, this, &AMRPlayerController::OnRestKey);
	Input->BindAction(ApplyAction, ETriggerEvent::Started, this, &AMRPlayerController::OnApplyKey);
	for (int32 i = 0; i < QuickChatActions.Num(); ++i)
	{
		Input->BindAction(QuickChatActions[i], ETriggerEvent::Started, this, &AMRPlayerController::OnQuickChatKey, i);
	}
	Input->BindAction(WhoAction, ETriggerEvent::Started, this, &AMRPlayerController::OnWindowKey, EMRWindow::Who);
	Input->BindAction(MailAction, ETriggerEvent::Started, this, &AMRPlayerController::OnWindowKey, EMRWindow::Mail);
	Input->BindAction(GuildAction, ETriggerEvent::Started, this, &AMRPlayerController::OnWindowKey, EMRWindow::Guild);
	Input->BindAction(MapWindowAction, ETriggerEvent::Started, this, &AMRPlayerController::OnWindowKey, EMRWindow::Map);
}

UMRUISubsystem* AMRPlayerController::GetUI() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
}

void AMRPlayerController::OnHotbarKey(int32 Index)
{
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->OnHotbarKey(Index);
	}
}

void AMRPlayerController::OnHotbarScroll(const FInputActionValue& Value)
{
	// Ctrl + wheel zooms the camera (AMRCharacter::OnZoom)
	if (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl))
	{
		return;
	}
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->OnHotbarScroll(Value.Get<float>());
	}
}

void AMRPlayerController::OnSpellKey(int32 Index)
{
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->OnSpellKey(Index);
	}
}

void AMRPlayerController::OnInventoryKey()
{
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->ToggleInventory();
	}
}

void AMRPlayerController::OnWindowKey(EMRWindow Window)
{
	const UMRNetWorldSubsystem* Net = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	UMRUISubsystem* UI = GetUI();
	if (!Net || !Net->IsActive() || !UI)
	{
		return;
	}
	if (Window == EMRWindow::Guild && !UI->IsWindowOpen(Window))
	{
		UI->RunChatLine(TEXT("/guild"));  // asks the server for the guild, then opens
		return;
	}
	UI->ToggleWindow(Window);
}

void AMRPlayerController::OnChatKey()
{
	const UMRNetWorldSubsystem* Net = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	UMRUISubsystem* UI = GetUI();
	if (Net && Net->IsActive() && UI)
	{
		UI->OpenChat();
	}
}

void AMRPlayerController::OnMenuKey()
{
	// the original's Escape: stop choosing a spell's or item's target (A_ENDSELECT), clear the
	// target (intrface.c A_TARGETCLEAR), then the menu
	UMRNetWorldSubsystem* NetWorld = GetNetWorld();
	UMRUISubsystem* UI = GetUI();
	if (NetWorld && NetWorld->IsChoosingTarget() && !(UI && UI->IsGameMenuOpen()))
	{
		NetWorld->CancelChoosing();
		return;
	}
	if (NetWorld && NetWorld->GetTargetId() && !(UI && UI->IsGameMenuOpen()))
	{
		NetWorld->ClearTarget();
		return;
	}
	if (UI)
	{
		UI->ToggleGameMenu();
	}
}

UMRNetWorldSubsystem* AMRPlayerController::GetNetWorld() const
{
	UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	return NetWorld && NetWorld->IsActive() ? NetWorld : nullptr;
}

void AMRPlayerController::OnTargetNext()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		// Shift+Tab goes back
		NetWorld->TargetNextOrPrevious(!(IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)));
	}
}

void AMRPlayerController::OnTargetPrevious()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		NetWorld->TargetNextOrPrevious(false);
	}
}

void AMRPlayerController::OnTargetSelf()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		// choosing a spell's or item's target: ourselves
		if (NetWorld->IsChoosingTarget())
		{
			if (const UMRNetSubsystem* Net = GetGameInstance()->GetSubsystem<UMRNetSubsystem>())
			{
				NetWorld->ChooseTarget(Net->GetPlayer().Id);
			}
			return;
		}
		NetWorld->TargetSelf();
	}
}

void AMRPlayerController::OnTargetAim()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		NetWorld->TargetAim();
	}
}

void AMRPlayerController::OnLookKey()
{
	UMRNetWorldSubsystem* NetWorld = GetNetWorld();
	if (!NetWorld)
	{
		return;
	}
	// several things under the crosshair (a pile of items): let the player pick (UMRUISubsystem)
	const TArray<uint32> Stack = NetWorld->ObjectsAtAim();
	if (!NetWorld->GetTargetId() && Stack.Num() > 1)
	{
		if (UMRUISubsystem* UI = GetUI())
		{
			UI->ShowLookPicker(Stack);
			return;
		}
	}
	NetWorld->LookAtTarget();
}

void AMRPlayerController::OnRestKey()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		NetWorld->SetResting(!NetWorld->IsResting());
	}
}

void AMRPlayerController::OnApplyKey()
{
	UMRNetWorldSubsystem* NetWorld = GetNetWorld();
	UMRUISubsystem* UI = GetUI();
	if (!NetWorld || !UI)
	{
		return;
	}
	if (const uint32 Item = UI->ItemToApply())
	{
		NetWorld->ApplyItem(Item);
	}
}

void AMRPlayerController::OnGetKey()
{
	UMRNetWorldSubsystem* NetWorld = GetNetWorld();
	UMRUISubsystem* UI = GetUI();
	if (!NetWorld || !UI)
	{
		return;
	}
	const TArray<uint32> Gettable = NetWorld->GettableAtAim();
	if (Gettable.Num() == 1)
	{
		UI->PickChosen(EMRPickAction::Get, Gettable[0]);
	}
	else if (Gettable.Num() > 1)
	{
		UI->ShowPicker(Gettable, EMRPickAction::Get);  // a pile: which one
	}
}

void AMRPlayerController::OnUseKey()
{
	if (UMRNetWorldSubsystem* NetWorld = GetNetWorld())
	{
		NetWorld->UseAim();
	}
}

void AMRPlayerController::OnMapZoom(const FInputActionValue& Value)
{
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->OnMapZoom(Value.Get<float>());
	}
}

void AMRPlayerController::MRBookmark(const FString& Name)
{
	FVector Location;
	FRotator Rotation;
	GetPlayerViewPoint(Location, Rotation);
	const float Fov = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 90.f;
	const FString Entry = UMRLookDevTour::FormatBookmark(Name.IsEmpty() ? TEXT("bookmark") : Name, Location, Rotation, Fov);
	UE_LOG(LogMeridian, Display, TEXT("MRBookmark: %s"), *Entry);
	ClientMessage(Entry);
}

void AMRPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
}
