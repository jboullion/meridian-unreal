#include "Player/MRPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UI/MRUISubsystem.h"
#include "MeridianRemastered.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Player/MRPlayerState.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRSpriteNetTest.h"
#include "Tests/MRMonsterTour.h"
#include "Tests/MRMapCapture.h"
#include "Tests/MRMoveTest.h"
#include "Tests/MRNetTest.h"
#include "Tests/MRUIShots.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	/** How long a server ClientPrepareZone request keeps a zone streamed in. */
	constexpr double PrepareZoneSeconds = 15.0;

	/** How long a zone stays resident after it stops being the current zone or a neighbour. */
	constexpr double RetainSeconds = 30.0;
}

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
		else if (UMRSpriteNetTest::IsRequested())
		{
			SpriteNetTest = NewObject<UMRSpriteNetTest>(this);
			SpriteNetTest->Start(this);
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
	const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	const FKey NumPad[] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
		EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
	for (int32 i = 0; i < 9; ++i)
	{
		UInputAction* Hotbar = MakeAction(FString::Printf(TEXT("IA_Hotbar%d"), i + 1), EInputActionValueType::Boolean);
		UIContext->MapKey(Hotbar, Numbers[i]);
		HotbarActions.Add(Hotbar);
		UInputAction* Spell = MakeAction(FString::Printf(TEXT("IA_Spell%d"), i + 1), EInputActionValueType::Boolean);
		UIContext->MapKey(Spell, NumPad[i]);
		SpellActions.Add(Spell);
	}
	HotbarScrollAction = MakeAction(TEXT("IA_HotbarScroll"), EInputActionValueType::Axis1D);
	UIContext->MapKey(HotbarScrollAction, EKeys::MouseWheelAxis);
	InventoryAction = MakeAction(TEXT("IA_Inventory"), EInputActionValueType::Boolean);
	UIContext->MapKey(InventoryAction, EKeys::E);
	UIContext->MapKey(InventoryAction, EKeys::I);
	ChatAction = MakeAction(TEXT("IA_Chat"), EInputActionValueType::Boolean);
	UIContext->MapKey(ChatAction, EKeys::Enter);
	MapZoomAction = MakeAction(TEXT("IA_MapZoom"), EInputActionValueType::Axis1D);
	UIContext->MapKey(MapZoomAction, EKeys::Equals);
	UIContext->MapKey(MapZoomAction, EKeys::Add);
	UIContext->MapKey(MapZoomAction, EKeys::Hyphen).Modifiers.Add(NewObject<UInputModifierNegate>(UIContext));
	UIContext->MapKey(MapZoomAction, EKeys::Subtract).Modifiers.Add(NewObject<UInputModifierNegate>(UIContext));
	MenuAction = MakeAction(TEXT("IA_Menu"), EInputActionValueType::Boolean);
	UIContext->MapKey(MenuAction, EKeys::Escape);
	UIContext->MapKey(MenuAction, EKeys::F10);
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
	if (UMRUISubsystem* UI = GetUI())
	{
		UI->ToggleGameMenu();
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
	// The server (dedicated, listen or standalone) keeps every zone loaded itself.
	if (IsLocalController() && GetWorld()->GetNetMode() == NM_Client)
	{
		UpdateZoneStreaming();
	}
}

void AMRPlayerController::ClientPrepareZone_Implementation(int32 Rid)
{
	PreparedZones.Add(Rid, FPlatformTime::Seconds() + PrepareZoneSeconds);
	UE_LOG(LogMeridian, Log, TEXT("MRStreaming: server asked to prepare zone %d"), Rid);
	UpdateZoneStreaming();
}

void AMRPlayerController::UpdateZoneStreaming()
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const AMRPlayerState* PS = GetPlayerState<AMRPlayerState>();
	if (!Zones || !Zones->IsLoaded())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const int32 Current = PS ? PS->GetZoneId() : 0;

	// what should be resident: current zone, its neighbours, and live server requests
	TSet<int32> Target;
	if (const FMRZoneInfo* Z = Zones->FindZone(Current))
	{
		Target.Add(Current);
		Target.Append(Z->Neighbours);
	}
	for (auto It = PreparedZones.CreateIterator(); It; ++It)
	{
		if (It->Value < Now)
		{
			It.RemoveCurrent();
		}
		else
		{
			Target.Add(It->Key);
		}
	}
	// Zones that just dropped out of the set stay resident for a while, so popping into a shop
	// and straight back out doesn't unload and reload the rest of the town.
	const TSet<int32> Core = Target;
	for (const int32 Rid : StreamingTarget)
	{
		if (!Core.Contains(Rid) && !RetainUntil.Contains(Rid))
		{
			RetainUntil.Add(Rid, Now + RetainSeconds);
		}
	}
	for (auto It = RetainUntil.CreateIterator(); It; ++It)
	{
		if (Core.Contains(It->Key) || It->Value < Now)
		{
			It.RemoveCurrent();
		}
		else
		{
			Target.Add(It->Key);
		}
	}

	if (!Target.Difference(StreamingTarget).IsEmpty() || !StreamingTarget.Difference(Target).IsEmpty())
	{
		for (const int32 Rid : Target)
		{
			if (!StreamingTarget.Contains(Rid) && !Zones->IsZoneVisibleLocally(Rid))
			{
				PendingLoads.Add(Rid, Now);
			}
		}
		StreamingTarget = Target;
		Zones->SetClientStreamingTarget(Target);
	}

	// load-time logging
	for (auto It = PendingLoads.CreateIterator(); It; ++It)
	{
		if (Zones->IsZoneVisibleLocally(It->Key))
		{
			UE_LOG(LogMeridian, Log, TEXT("MRStreaming: zone %d visible after %.0f ms"), It->Key, (Now - It->Value) * 1000.0);
			It.RemoveCurrent();
		}
		else if (!Target.Contains(It->Key))
		{
			It.RemoveCurrent();
		}
	}

	// Entering a zone: was its geometry already there? (It should be: it was a preloaded
	// neighbour, or the server waited for it.) "ready=0" here means a visible hitch.
	// Only counts once the pawn exists: the server sets the start zone before spawning and
	// holds the spawn until the client has streamed it.
	if (Current != LastZone && Current != 0 && GetPawn())
	{
		UE_LOG(LogMeridian, Log, TEXT("MRStreaming: entered zone %d, ready=%d"), Current, Zones->IsZoneVisibleLocally(Current) ? 1 : 0);
		LastZone = Current;
	}
}
