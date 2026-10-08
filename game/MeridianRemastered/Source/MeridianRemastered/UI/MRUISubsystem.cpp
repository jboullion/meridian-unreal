#include "UI/MRUISubsystem.h"

#include "Abilities/MRAttributeSet.h"
#include "Brushes/SlateNoResource.h"
#include "Character/MRCharacter.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Net/MRNetSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Zones/MRZoneSubsystem.h"
#include "Player/MRPlayerState.h"
#include "UI/MRAvatarPreview.h"
#include "UI/MRGameData.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/SMRHUD.h"
#include "UI/SMRInventoryScreen.h"
#include "UI/SMRCharCreator.h"
#include "UI/SMRLoginScreen.h"
#include "UI/SMRLookDialog.h"
#include "Net/MRAssetCache.h"
#include "Net/MRNetLook.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "UI/SMRMinimap.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRUI"

namespace
{
	constexpr float SpellCooldownSeconds = 1.5f;  // mock: the spell bar's sweep after a cast
}

void UMRUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// UMRUIStyle and UMRGameDataSubsystem are game instance subsystems: already up before a local player's
	Super::Initialize(Collection);

	UMRMockInventory* MockInventory = NewObject<UMRMockInventory>(this);
	MockInventory->SetData(GetData());
	MockInventory->LoadFromJson();
	Mock = MockInventory;
	UseSource(Mock);

	if (UMRUIStyle* Style = GetStyle())
	{
		StyleHandle = Style->OnReloaded.AddUObject(this, &UMRUISubsystem::OnStyleReloaded);
	}
	if (UMRNetSubsystem* Net = GetNet())
	{
		NetStatsHandle = Net->OnStatsChanged.AddUObject(this, &UMRUISubsystem::OnNetStats);
		NetPhaseHandle = Net->OnPhaseChanged.AddUObject(this, &UMRUISubsystem::OnNetPhase);
		NetDescriptionHandle = Net->OnDescription.AddUObject(this, &UMRUISubsystem::OnNetDescription);
		NetContentsHandle = Net->OnContents.AddUObject(this, &UMRUISubsystem::OnNetContents);
	}
}

void UMRUISubsystem::UseSource(UMRInventorySource* InSource)
{
	if (Source == InSource)
	{
		return;
	}
	if (Source)
	{
		Source->ReturnCursor();
		Source->OnSelectionChanged.RemoveAll(this);
	}
	Source = InSource;
	if (Source)
	{
		Source->OnSelectionChanged.AddWeakLambda(this, [this]() { SelectionTime = Now(); });
	}
	HoveredSlot = PressSlot = FMRSlotRef();
	++StatsVersion;
}

void UMRUISubsystem::OnNetPhase()
{
	const UMRNetSubsystem* Net = GetNet();
	const EMRNetPhase Phase = Net ? Net->GetPhase() : EMRNetPhase::Offline;
	if (Phase == EMRNetPhase::InGame && !Cast<UMRNetInventory>(Source))
	{
		// a character entered a server's game: its own spell bar, its spells and skills as the server sends them
		UMRNetInventory* NetInventory = NewObject<UMRNetInventory>(this);
		NetInventory->SetData(GetData());
		NetInventory->SetNet(GetNet());
		UseSource(NetInventory);
		OnNetStats(3);
		OnNetStats(4);
	}
	else if (Phase != EMRNetPhase::InGame && Source != Mock)
	{
		UseSource(Mock);
	}
	if (Phase != EMRNetPhase::InGame)
	{
		bGameMenuOpen = false;
		bLookOpen = false;
	}
}

// ------------------------------------------------------------------------------ look

TSharedPtr<SMRLookDialog> UMRUISubsystem::GetLookDialog() const
{
	return HUD.IsValid() ? HUD->GetLookDialog() : nullptr;
}

void UMRUISubsystem::SetLookOpen(bool bOpen)
{
	if (!HUD.IsValid())
	{
		return;
	}
	if (bOpen)
	{
		SetInventoryOpen(false);
		SetGameMenuOpen(false);
	}
	if (!bOpen && bLookOpen && !Login.IsValid())
	{
		SetCreatorCapturing(false);  // the portrait of a player looked at
	}
	bLookOpen = bOpen;
	HUD->SetLookOpen(bOpen);
	ApplyInputMode();
}

void UMRUISubsystem::CloseLook()
{
	SetLookOpen(false);
}

void UMRUISubsystem::LookAt(uint32 ObjectId)
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->RequestLook(ObjectId);
	}
}

void UMRUISubsystem::SaveDescription(uint32 ObjectId, const FString& Text)
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->ChangeDescription(ObjectId, Text);
		Net->RequestLook(ObjectId);  // show what the server now has
	}
}

void UMRUISubsystem::ShowLookPicker(const TArray<uint32>& Ids)
{
	ShowPicker(Ids, EMRPickAction::Look);
}

namespace
{
	/** A thing's name in a list: a number item with its amount ("57 shillings"). */
	FText ListName(const FMRNetObject* O, uint32 Id)
	{
		if (!O)
		{
			return FText::FromString(FString::Printf(TEXT("#%u"), Id));
		}
		return FText::FromString(O->bNumber ? FString::Printf(TEXT("%u %s"), O->Amount, *O->Name) : O->Name);
	}
}

void UMRUISubsystem::ShowPicker(const TArray<uint32>& Ids, EMRPickAction Action)
{
	const UMRNetSubsystem* Net = GetNet();
	TSharedPtr<SMRLookDialog> Dialog = GetLookDialog();
	if (!Net || !Dialog.IsValid())
	{
		return;
	}
	TArray<FText> Names;
	for (const uint32 Id : Ids)
	{
		Names.Add(ListName(Net->FindObject(Id), Id));
	}
	Dialog->ShowPicker(Ids, Names, Action, Action == EMRPickAction::Get ? NSLOCTEXT("MRLook", "PickGet", "Pick up...")
		: NSLOCTEXT("MRLook", "PickTitle", "Look at..."));
	SetLookOpen(true);
}

void UMRUISubsystem::PickChosen(EMRPickAction Action, uint32 ObjectId)
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	switch (Action)
	{
	case EMRPickAction::Look:
		LookAt(ObjectId);
		break;
	case EMRPickAction::Get:
		Net->Pickup(ObjectId);
		CloseLook();
		break;
	case EMRPickAction::GetFromContainer:
	{
		// all of it; then the container again, to show what's left
		const uint32 Container = Net->GetNetWorld().ContentsOf;
		Net->PickupFromContainer(ObjectId);
		Net->RequestContents(Container);
		break;
	}
	}
}

void UMRUISubsystem::DoObjectAction(EMRObjectAction Action, uint32 ObjectId)
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	switch (Action)
	{
	case EMRObjectAction::Get:
		Net->Pickup(ObjectId);
		CloseLook();
		break;
	case EMRObjectAction::Inside:
		Net->RequestContents(ObjectId);
		break;
	case EMRObjectAction::Activate:
		Net->Activate(ObjectId);
		CloseLook();
		break;
	}
}

void UMRUISubsystem::OnNetContents()
{
	const UMRNetSubsystem* Net = GetNet();
	TSharedPtr<SMRLookDialog> Dialog = GetLookDialog();
	if (!Net || !Dialog.IsValid())
	{
		return;
	}
	const FMRNetWorld& W = Net->GetNetWorld();
	TArray<uint32> Ids;
	TArray<FText> Names;
	for (const FMRNetObject& O : W.Contents)
	{
		Ids.Add(O.Id);
		Names.Add(ListName(&O, O.Id));
	}
	const FMRNetObject* Container = Net->FindObject(W.ContentsOf);
	const FText Title = Container ? FText::FromString(Container->Name) : NSLOCTEXT("MRLook", "Inside", "Inside");
	if (Ids.IsEmpty())
	{
		Names.Add(NSLOCTEXT("MRLook", "Empty", "(nothing)"));
		Ids.Add(0);
	}
	Dialog->ShowPicker(Ids, Names, EMRPickAction::GetFromContainer, Title);
	SetLookOpen(true);
}

void UMRUISubsystem::OnNetDescription()
{
	UMRNetSubsystem* Net = GetNet();
	TSharedPtr<SMRLookDialog> Dialog = GetLookDialog();
	if (!Net || !Dialog.IsValid())
	{
		return;
	}
	const FMRNetDescription& D = Net->GetDescription();
	UE_LOG(LogMeridian, Log, TEXT("UI: looking at %s (%s)"), *D.Object.Name, D.bPlayer ? TEXT("a player") : *D.Object.Icon);
	LookBrush = FSlateBrush();
	LookBrush.DrawAs = ESlateBrushDrawType::NoDrawType;  // nothing until the picture is there (an empty brush draws white)
	LookPicture = nullptr;
	LookPictureFor = D.Object.Id;
	FMRSpriteAppearance A;
	if (D.bPlayer && MRNetLook::AppearanceFromObject(D.Object, A))
	{
		// a player: their face, from the creator's portrait preview
		SetCreatorAppearance(A);
		SetCreatorCapturing(true);
		if (UObject* Target = GetCreatorTarget(true))
		{
			UMRUIStyle::SetImage(LookBrush, Target, FVector2f(256.f, 256.f));
		}
	}
	else if (FMRAssetCache* Cache = Net->GetAssets(); Cache && Cache->IsListed(D.Object.Icon))
	{
		// anything else: its own bitmap, its current group seen from the front
		const int32 Group = FMath::Max<int32>(1, D.Object.Animation.Type == MRMsg::ANIMATE_NONE ? D.Object.Animation.Group : D.Object.Animation.GroupLow) - 1;
		const uint32 For = D.Object.Id;
		TWeakObjectPtr<UMRUISubsystem> Weak(this);
		Cache->Fetch(D.Object.Icon, [Weak, For, Group](bool bOk, const TArray<uint8>& Bytes)
		{
			UMRUISubsystem* Self = Weak.Get();
			FMRBgf Bgf;
			FString Error;
			if (!Self || Self->LookPictureFor != For || !bOk || !Bgf.Load(Bytes, Error))
			{
				return;
			}
			const int32 Bitmap = Bgf.Groups.IsValidIndex(Group) && Bgf.Groups[Group].Num() > 0 ? Bgf.Groups[Group][0] : 0;
			if (UTexture2D* Tex = Bgf.MakeTexture(Bgf.Bitmaps.IsValidIndex(Bitmap) ? Bitmap : 0, false))
			{
				Tex->AddressX = TA_Clamp;
				Tex->AddressY = TA_Clamp;
				Tex->UpdateResource();
				Self->LookPicture = Tex;
				// fit the box, keeping its shape (the box is square)
				const float Side = FMath::Max(Tex->GetSizeX(), Tex->GetSizeY());
				UMRUIStyle::SetImage(Self->LookBrush, Tex, FVector2f(Tex->GetSizeX(), Tex->GetSizeY()) * (256.f / Side));
			}
		});
	}
	Dialog->ShowDescription(D, &LookBrush);
	SetLookOpen(true);
}

void UMRUISubsystem::Deinitialize()
{
	HideLogin();
	RemoveHUD();
	if (UMRUIStyle* Style = GetStyle())
	{
		Style->OnReloaded.Remove(StyleHandle);
	}
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->OnStatsChanged.Remove(NetStatsHandle);
		Net->OnPhaseChanged.Remove(NetPhaseHandle);
		Net->OnDescription.Remove(NetDescriptionHandle);
		Net->OnContents.Remove(NetContentsHandle);
	}
	Super::Deinitialize();
}

UMRUIStyle* UMRUISubsystem::GetStyle() const
{
	const UGameInstance* GI = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRUIStyle>() : nullptr;
}

UMRNetSubsystem* UMRUISubsystem::GetNet() const
{
	const UGameInstance* GI = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
}

UMRGameDataSubsystem* UMRUISubsystem::GetData() const
{
	const UGameInstance* GI = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRGameDataSubsystem>() : nullptr;
}

APlayerController* UMRUISubsystem::GetPlayerController() const
{
	if (OwnerPC.IsValid())
	{
		return OwnerPC.Get();
	}
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
}

UMRAttributeSet* UMRUISubsystem::GetAttributes() const
{
	const APlayerController* PC = GetPlayerController();
	const AMRPlayerState* PS = PC ? PC->GetPlayerState<AMRPlayerState>() : nullptr;
	return PS ? PS->GetAttributeSet() : nullptr;
}

double UMRUISubsystem::Now() const
{
	return FPlatformTime::Seconds();
}

// ------------------------------------------------------------------------------ login

void UMRUISubsystem::ShowLogin(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
	{
		return;
	}
	UGameViewportClient* Viewport = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		return;
	}
	OwnerPC = PC;
	if (!Login.IsValid())
	{
		Login = SNew(SMRLoginScreen, this);
		Viewport->AddViewportWidgetForPlayer(GetLocalPlayer(), Login.ToSharedRef(), 20);
		UE_LOG(LogMeridian, Log, TEXT("UI: login screen shown"));
	}
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Login);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->bShowMouseCursor = true;
	Login->OnShown();
}

void UMRUISubsystem::HideLogin()
{
	if (!Login.IsValid())
	{
		return;
	}
	APlayerController* PC = GetPlayerController();
	if (UGameViewportClient* Viewport = PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetForPlayer(GetLocalPlayer(), Login.ToSharedRef());
	}
	Login.Reset();
	ApplyInputMode();
}

// ------------------------------------------------------------------------------ chat

void UMRUISubsystem::OpenChat()
{
	if (!HUD.IsValid() || bChatOpen)
	{
		return;
	}
	if (bInventoryOpen)
	{
		SetInventoryOpen(false);
	}
	if (bGameMenuOpen || bLookOpen)
	{
		return;
	}
	bChatOpen = true;
	ApplyInputMode();
	HUD->OpenChat();
}

void UMRUISubsystem::OnChatClosed()
{
	bChatOpen = false;
	ApplyInputMode();
}

// ------------------------------------------------------------------------------ HUD

void UMRUISubsystem::ShowHUD(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
	{
		return;
	}
	UGameViewportClient* Viewport = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		return;
	}
	if (HUD.IsValid())
	{
		if (OwnerPC.Get() == PC)
		{
			return;
		}
		RemoveHUD();
	}
	OwnerPC = PC;
	HUD = SNew(SMRHUDRoot, this);
	Viewport->AddViewportWidgetForPlayer(GetLocalPlayer(), HUD.ToSharedRef(), 10);
	UE_LOG(LogMeridian, Log, TEXT("UI: HUD shown"));
}

void UMRUISubsystem::RemoveHUD()
{
	if (!HUD.IsValid())
	{
		return;
	}
	if (APlayerController* PC = OwnerPC.Get())
	{
		if (UGameViewportClient* Viewport = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetForPlayer(GetLocalPlayer(), HUD.ToSharedRef());
		}
	}
	HUD.Reset();
	bInventoryOpen = false;
	bChatOpen = false;
	bGameMenuOpen = false;
	bLookOpen = false;
	if (Avatar)
	{
		Avatar->Destroy();
		Avatar = nullptr;
	}
}

void UMRUISubsystem::OnStyleReloaded()
{
	if (HUD.IsValid())
	{
		HUD->Rebuild();
		HUD->SetInventoryOpen(bInventoryOpen);
	}
}

// ------------------------------------------------------------------------------ keys

void UMRUISubsystem::OnHotbarKey(int32 Index)
{
	if (Source)
	{
		Source->SelectHotbar(Index);
	}
}

void UMRUISubsystem::OnHotbarScroll(float Delta)
{
	if (Source && !FMath::IsNearlyZero(Delta) && !bInventoryOpen)
	{
		// wheel down moves right, as Minecraft
		Source->SelectHotbar(Source->GetSelectedHotbar() + (Delta < 0.f ? 1 : -1));
	}
}

void UMRUISubsystem::OnSpellKey(int32 Index)
{
	if (!Source || Index < 0 || Index >= 9)
	{
		return;
	}
	LastSpellKeyTime = Now();
	const FMRSlotContent C = Source->Get(FMRSlotRef(EMRSlotArea::SpellBar, Index));
	if (C.IsEmpty() || SpellCooldown(Index) > 0.f)
	{
		return;
	}
	// mock cast: the sprite's cast action and the slot's cooldown sweep (spells come with the server)
	SpellCastTime[Index] = Now();
	if (APlayerController* PC = GetPlayerController())
	{
		if (AMRCharacter* Char = Cast<AMRCharacter>(PC->GetPawn()))
		{
			Char->PlaySpriteAction(TEXT("cast"));
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("UI: cast %s (mock)"), *C.Id.ToString());
}

float UMRUISubsystem::SpellCooldown(int32 Index) const
{
	if (Index < 0 || Index >= 9)
	{
		return 0.f;
	}
	const double Since = Now() - SpellCastTime[Index];
	return Since >= 0.0 && Since < SpellCooldownSeconds && SpellCastTime[Index] > 0.0 ? 1.f - static_cast<float>(Since / SpellCooldownSeconds) : 0.f;
}

void UMRUISubsystem::OnMapZoom(float Steps)
{
	MapZoom = FMath::Clamp(MapZoom * FMath::Pow(1.25f, Steps), 0.4f, 4.f);
}

void UMRUISubsystem::ToggleInventory()
{
	SetInventoryOpen(!bInventoryOpen);
}

void UMRUISubsystem::ToggleGameMenu()
{
	SetGameMenuOpen(!bGameMenuOpen);
}

void UMRUISubsystem::SetGameMenuOpen(bool bOpen)
{
	if (!HUD.IsValid() || bOpen == bGameMenuOpen)
	{
		return;
	}
	if (bOpen)
	{
		SetInventoryOpen(false);
		if (bChatOpen)
		{
			return;  // Esc belongs to the chat line while typing
		}
	}
	bGameMenuOpen = bOpen;
	ApplyInputMode();
	HUD->SetGameMenuOpen(bOpen);
}

bool UMRUISubsystem::CanLogOff() const
{
	const UMRNetSubsystem* Net = GetNet();
	return Net && Net->GetPhase() == EMRNetPhase::InGame;
}

void UMRUISubsystem::LogOffToCharacters()
{
	SetGameMenuOpen(false);
	if (UMRNetSubsystem* Net = GetNet(); Net && CanLogOff())
	{
		UE_LOG(LogMeridian, Log, TEXT("UI: log off to the character list"));
		Net->ReturnToCharacters();
	}
}

void UMRUISubsystem::QuitGame()
{
	SetGameMenuOpen(false);
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->Logoff();  // BP_REQ_QUIT first: the character leaves the world cleanly
	}
	APlayerController* PC = GetPlayerController();
	UKismetSystemLibrary::QuitGame(PC, PC, EQuitPreference::Quit, false);
}

void UMRUISubsystem::SetInventoryOpen(bool bOpen)
{
	if (!HUD.IsValid() || bOpen == bInventoryOpen || (bOpen && bGameMenuOpen))
	{
		return;
	}
	bInventoryOpen = bOpen;
	bTextInput = false;
	if (!bOpen && Source)
	{
		Source->ReturnCursor();  // nothing stays stuck on the mouse
		HoveredSlot = PressSlot = FMRSlotRef();
	}
	if (bOpen)
	{
		EnsureAvatar();
	}
	if (Avatar)
	{
		Avatar->SetCapturing(bOpen);
	}
	ApplyInputMode();
	HUD->SetInventoryOpen(bOpen);
}

void UMRUISubsystem::SetInventoryTab(int32 Tab)
{
	if (HUD.IsValid())
	{
		HUD->SetInventoryTab(Tab);
	}
}

void UMRUISubsystem::DebugSearch(int32 Tab, const FString& Text)
{
	if (HUD.IsValid() && HUD->GetInventory().IsValid())
	{
		HUD->GetInventory()->DebugSearch(static_cast<EMRInventoryTab>(Tab), Text);
	}
}

void UMRUISubsystem::DebugFoldSpellSchool(int32 School)
{
	if (HUD.IsValid() && HUD->GetInventory().IsValid())
	{
		HUD->GetInventory()->DebugFoldSpellSchool(School);
	}
}

void UMRUISubsystem::ApplyInputMode()
{
	APlayerController* PC = GetPlayerController();
	if (!PC)
	{
		return;
	}
	if (Login.IsValid())
	{
		return;  // the login screen owns the input (ShowLogin)
	}
	if (bLookOpen && !bGameMenuOpen)
	{
		// reading (or writing one's description): the dialog has the keyboard and the mouse
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		PC->SetIgnoreLookInput(true);
	}
	else if (bGameMenuOpen)
	{
		// the menu has the keyboard (Esc closes it); nothing walks or looks meanwhile
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		PC->SetIgnoreLookInput(true);
	}
	else if (bInventoryOpen && bTextInput)
	{
		// typing in the dialog (the spell search): every key goes to the text box, the cursor stays
		PC->SetInputMode(FInputModeUIOnly());
		PC->bShowMouseCursor = true;
		PC->SetIgnoreLookInput(true);
	}
	else if (bChatOpen)
	{
		// typing: every key goes to the chat line (no walking off while you write)
		PC->SetInputMode(FInputModeUIOnly());
		PC->bShowMouseCursor = false;
		PC->SetIgnoreLookInput(true);
	}
	else if (bInventoryOpen)
	{
		// the cursor for the window; WASD still walks (an online game doesn't pause), the mouse doesn't look
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		PC->SetIgnoreLookInput(true);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		PC->ResetIgnoreLookInput();
	}
}

// ------------------------------------------------------------------------------ slots

void UMRUISubsystem::OnSlotMouseDown(const FMRSlotRef& Slot, bool bRight, bool bShift)
{
	if (!Source)
	{
		return;
	}
	const bool bCarrying = !Source->Get(FMRSlotRef(EMRSlotArea::Cursor, 0)).IsEmpty();
	if (bShift && !bCarrying)
	{
		Source->QuickMove(Slot);
		PressSlot = FMRSlotRef();
		return;
	}
	Source->Click(Slot, bRight);
	// picked something up with this press: releasing it over another slot drops it there (drag and drop)
	bPickedOnPress = !bCarrying && !Source->Get(FMRSlotRef(EMRSlotArea::Cursor, 0)).IsEmpty() && !bRight;
	PressSlot = Slot;
}

void UMRUISubsystem::OnSlotMouseUp(const FMRSlotRef& Slot)
{
	if (Source && bPickedOnPress && PressSlot.IsValid() && Slot != PressSlot)
	{
		Source->Click(Slot, false);
	}
	bPickedOnPress = false;
	PressSlot = FMRSlotRef();
}

void UMRUISubsystem::SetHoveredSlot(const FMRSlotRef& Slot, bool bHovered)
{
	if (bHovered)
	{
		HoveredSlot = Slot;
	}
	else if (HoveredSlot == Slot)
	{
		HoveredSlot = FMRSlotRef();
	}
}

void UMRUISubsystem::OnClickOutside(bool bRight)
{
	if (Source)
	{
		Source->DropCursor(bRight);
	}
}

// ------------------------------------------------------------------------------ content

const FSlateBrush* UMRUISubsystem::IconFor(const FMRSlotContent& C) const
{
	UMRUIStyle* Style = GetStyle();
	UMRGameDataSubsystem* Data = GetData();
	if (!Style || !Data || C.IsEmpty())
	{
		return nullptr;
	}
	if (C.bSpell)
	{
		const FMRSpellDef* Spell = Data->FindSpell(C.Id);
		return Spell ? Style->Icon(Spell->Icon) : nullptr;
	}
	const FMRItemDef* Item = Data->FindItem(C.Id);
	const FSlateBrush* Icon = Item ? Style->Icon(Item->Icon) : nullptr;
	if (!Icon && C.ObjectId)
	{
		// a server item without a prebuilt icon: its own bitmap, in the group it shows in an inventory
		if (const UMRNetInventory* NetInv = Cast<UMRNetInventory>(Source))
		{
			if (const FMRNetObject* O = NetInv->FindObject(C))
			{
				const int32 Group = O->Animation.Type == MRMsg::ANIMATE_NONE ? O->Animation.Group : O->Animation.GroupLow;
				Icon = BitmapIcon(O->Icon, FMath::Max(1, Group));
			}
		}
	}
	return Icon;
}

const FSlateBrush* UMRUISubsystem::BitmapIcon(const FString& Bgf, int32 Group) const
{
	const FString Key = FString::Printf(TEXT("%s:%d"), *Bgf.ToLower(), Group);
	if (const TSharedPtr<FSlateBrush>* Found = BitmapIcons.Find(Key))
	{
		return Found->Get();
	}
	BitmapIcons.Add(Key, nullptr);
	UMRNetSubsystem* Net = GetNet();
	FMRAssetCache* Cache = Net ? Net->GetAssets() : nullptr;
	if (!Cache || !Cache->IsListed(Bgf))
	{
		return nullptr;
	}
	TWeakObjectPtr<const UMRUISubsystem> Weak(this);
	Cache->Fetch(Bgf, [Weak, Key, Group](bool bOk, const TArray<uint8>& Bytes)
	{
		const UMRUISubsystem* Self = Weak.Get();
		FMRBgf B;
		FString Error;
		if (!Self || !bOk || !B.Load(Bytes, Error))
		{
			return;
		}
		const int32 G = Group - 1;
		const int32 Bitmap = B.Groups.IsValidIndex(G) && B.Groups[G].Num() > 0 ? B.Groups[G][0] : 0;
		UTexture2D* Tex = B.MakeTexture(B.Bitmaps.IsValidIndex(Bitmap) ? Bitmap : 0, false);
		if (!Tex)
		{
			return;
		}
		Tex->Filter = TF_Nearest;  // the original pixels, square (docs/adr/0008)
		Tex->UpdateResource();
		Self->BitmapIconTextures.Add(Key, Tex);
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		UMRUIStyle::SetImage(*Brush, Tex, FVector2f(Tex->GetSizeX(), Tex->GetSizeY()));
		Self->BitmapIcons.Add(Key, Brush);
	});
	return nullptr;
}

FText UMRUISubsystem::NameFor(const FMRSlotContent& C) const
{
	UMRGameDataSubsystem* Data = GetData();
	if (!Data || C.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (C.bSpell)
	{
		const FMRSpellDef* Spell = Data->FindSpell(C.Id);
		return Spell ? Spell->Name : FText::FromName(C.Id);
	}
	if (const UMRNetInventory* NetInv = C.ObjectId ? Cast<UMRNetInventory>(Source) : nullptr)
	{
		if (const FMRNetObject* O = NetInv->FindObject(C))
		{
			return FText::FromString(O->Name);  // the server's (an identified item's real name)
		}
	}
	const FMRItemDef* Item = Data->FindItem(C.Id);
	return Item ? Item->Name : FText::FromName(C.Id);
}

TSharedPtr<IToolTip> UMRUISubsystem::MakeToolTip(const FMRSlotContent& C)
{
	UMRGameDataSubsystem* Data = GetData();
	if (!Data)
	{
		return nullptr;
	}
	FText Desc, Line;
	if (C.bSpell)
	{
		if (const FMRSpellDef* Spell = Data->FindSpell(C.Id))
		{
			Desc = Spell->Desc;
			const int32 Pct = Source ? Source->GetSpellPercent(C.Id) : -1;
			Line = FText::Format(LOCTEXT("TipSpell", "{0}, level {1}  ·  {2} mana{3}"), UMRGameDataSubsystem::SchoolName(Spell->School),
				FText::AsNumber(Spell->Level), FText::AsNumber(Spell->Mana),
				Pct >= 0 ? FText::Format(LOCTEXT("TipPct", "  ·  {0}%"), FText::AsNumber(Pct)) : FText::GetEmpty());
		}
	}
	else if (const FMRItemDef* Item = Data->FindItem(C.Id))
	{
		Desc = Item->Desc;
		FString Parts;
		if (Item->DamageMax > 0)
		{
			Parts += FString::Printf(TEXT("Damage %d-%d  ·  "), Item->DamageMin, Item->DamageMax);
		}
		if (Item->Defense > 0)
		{
			Parts += FString::Printf(TEXT("Defense %d  ·  "), Item->Defense);
		}
		Parts += FString::Printf(TEXT("Weight %d  ·  Bulk %d  ·  Value %d"), Item->Weight, Item->Bulk, Item->Value);
		Line = FText::FromString(Parts);
	}
	if (const UMRNetInventory* NetInv = C.ObjectId ? Cast<UMRNetInventory>(Source) : nullptr)
	{
		if (NetInv->IsInUse(C))
		{
			Line = FText::FromString(Line.IsEmpty() ? TEXT("In use") : TEXT("In use  ·  ") + Line.ToString());
		}
	}
	return FramedToolTip(NameFor(C), Line, Desc);
}

TSharedPtr<IToolTip> UMRUISubsystem::MakeSkillToolTip(FName Skill, int32 Percent)
{
	UMRGameDataSubsystem* Data = GetData();
	const FMRSkillDef* Def = Data ? Data->FindSkill(Skill) : nullptr;
	if (!Def)
	{
		return nullptr;
	}
	const FText Line = FText::Format(LOCTEXT("TipSkill", "{0}, level {1}  ·  {2}%"), UMRGameDataSubsystem::SchoolName(Def->School),
		FText::AsNumber(Def->Level), FText::AsNumber(Percent));
	return FramedToolTip(Def->Name, Line, Def->Desc);
}

TSharedPtr<IToolTip> UMRUISubsystem::FramedToolTip(const FText& TitleText, const FText& Line, const FText& Desc)
{
	UMRUIStyle* S = GetStyle();
	if (!S)
	{
		return nullptr;
	}
	const float Px = S->Px();
	const FLinearColor Title = S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f));
	const FLinearColor Body(0.82f, 0.8f, 0.74f);
	const FLinearColor Detail = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	auto Text = [S, Px](const FText& T, float Size, bool bBold, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(T).Font(S->Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(true)
			.ShadowOffset(FVector2D(1.0, 1.0) * Px * 0.5).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	};
	// the dialog's look: dark stone in the iron-and-vine frame (the tooltip's own border is empty)
	static const FSlateNoResource NoBorder;
	return SNew(SToolTip)
		.BorderImage(&NoBorder)
		.TextMargin(FMargin(0.f))
		[
			SNew(SMRPanel, this).Background(TEXT("invbkgnd")).Frame(TEXT("edge")).Padding(S->Number(TEXT("tooltip_padding_px"), 7.f))
			[
				SNew(SBox).MaxDesiredWidth(S->Number(TEXT("tooltip_width_px"), 170.f) * Px)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(TitleText, 11.f, true, Title)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[Text(Line, 8.f, false, Detail)]
					+ SVerticalBox::Slot().AutoHeight()[Text(Desc, 8.5f, false, Body)]
				]
			]
		];
}

// ------------------------------------------------------------------------------ stats

void UMRUISubsystem::SetTextInput(bool bTyping)
{
	if (bTextInput != bTyping)
	{
		bTextInput = bTyping;
		ApplyInputMode();
	}
}

void UMRUISubsystem::OnNetStats(uint32 Group)
{
	++StatsVersion;
	UMRNetSubsystem* Net = GetNet();
	UMRGameDataSubsystem* Data = GetData();
	UMRNetInventory* NetInventory = Cast<UMRNetInventory>(Source);
	const FMRNetStatGroup* G = Net ? Net->FindStatGroup(static_cast<uint8>(Group)) : nullptr;
	if (!G || !G->bReceived || !Data || !NetInventory || (Group != 3 && Group != 4))
	{
		return;
	}
	// Server 104's groups 3 and 4 are the character's spells and skills (a list stat per entry: its
	// name and ability percentage); our data gives school, level, icon and description by name
	TArray<FName> Spells;
	TMap<FName, int32> Percents;
	TArray<FString> Unknown;
	for (const FMRNetStat& S : G->Stats)
	{
		if (S.Type != FMRNetStat::List)
		{
			continue;
		}
		if (Group == 3)
		{
			if (const FMRSpellDef* Def = Data->FindSpellByName(S.Name))
			{
				Spells.Add(Def->Class);
				Percents.Add(Def->Class, S.Value);
				continue;
			}
		}
		else if (const FMRSkillDef* Def = Data->FindSkillByName(S.Name))
		{
			Percents.Add(Def->Class, S.Value);
			continue;
		}
		Unknown.Add(S.Name);
	}
	if (Unknown.Num() > 0)
	{
		UE_LOG(LogMeridian, Warning, TEXT("UI: server %s not in our data: %s"), Group == 3 ? TEXT("spells") : TEXT("skills"),
			*FString::Join(Unknown, TEXT(", ")));
	}
	if (Group == 3)
	{
		NetInventory->SetKnownSpells(Spells, Percents);
	}
	else
	{
		NetInventory->SetSkills(Percents);
	}
}

void UMRUISubsystem::GetStatSections(TArray<FMRStatSection>& Out) const
{
	Out.Reset();
	// the list: the server's (online) or the mock's
	TArray<FMRStatView> List;
	FString Ruleset;
	const UMRNetSubsystem* Net = GetNet();
	const FMRNetStatGroup* G = Net && Net->GetPhase() == EMRNetPhase::InGame ? Net->FindStatGroup(2) : nullptr;
	if (G && G->bReceived)
	{
		for (const FMRNetStat& S : G->Stats)
		{
			if (S.Type != FMRNetStat::Numeric)
			{
				continue;
			}
			FMRStatView V;
			V.Name = S.Name;
			V.Value = S.Value;
			V.Min = S.Min;
			V.Max = S.Tag == 1 ? (S.CurrentMax > 0 ? S.CurrentMax : S.Max) : 0;
			V.Text = S.ValueText;
			List.Add(V);
		}
		if (const FMRServerEntry* Server = Net->GetServer())
		{
			Ruleset = Server->Ruleset;
		}
	}
	else if (Source)
	{
		List = Source->GetStats();
	}

	// the layout for the ruleset
	auto Everything = [&Out, &List]()
	{
		FMRStatSection& All = Out.AddDefaulted_GetRef();
		All.Title = LOCTEXT("StatsAll", "Stats");
		All.Stats = List;
	};
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ui"), TEXT("stat_layout.json"));
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		Everything();
		return;
	}
	if (Ruleset.IsEmpty())
	{
		Root->TryGetStringField(TEXT("default"), Ruleset);
	}
	const TSharedPtr<FJsonObject>* Rulesets = nullptr;
	const TSharedPtr<FJsonObject>* Layout = nullptr;
	if (!Root->TryGetObjectField(TEXT("rulesets"), Rulesets) || !(*Rulesets)->TryGetObjectField(Ruleset, Layout))
	{
		Everything();
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Hide = nullptr;
	(*Layout)->TryGetArrayField(TEXT("hide"), Hide);
	const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
	(*Layout)->TryGetArrayField(TEXT("sections"), Sections);
	TArray<TArray<FString>> Patterns;
	if (Sections)
	{
		for (const TSharedPtr<FJsonValue>& SV : *Sections)
		{
			const TSharedPtr<FJsonObject> SO = SV->AsObject();
			if (!SO)
			{
				continue;
			}
			FMRStatSection& Sec = Out.AddDefaulted_GetRef();
			FString Title;
			SO->TryGetStringField(TEXT("title"), Title);
			Sec.Title = FText::FromString(Title);
			SO->TryGetBoolField(TEXT("points"), Sec.bPoints);
			TArray<FString>& P = Patterns.AddDefaulted_GetRef();
			SO->TryGetStringArrayField(TEXT("stats"), P);
		}
	}
	if (Out.Num() == 0)
	{
		Everything();
		return;
	}
	for (const FMRStatView& V : List)
	{
		bool bHidden = false;
		for (int32 i = 0; Hide && i < Hide->Num() && !bHidden; ++i)
		{
			bHidden = V.Name.MatchesWildcard((*Hide)[i]->AsString(), ESearchCase::IgnoreCase);
		}
		if (bHidden)
		{
			continue;
		}
		// the first section naming it outright, else the first whose pattern (with *) matches, else the last
		int32 Target = INDEX_NONE;
		for (int32 Pass = 0; Pass < 2 && Target == INDEX_NONE; ++Pass)
		{
			for (int32 i = 0; i < Patterns.Num() && Target == INDEX_NONE; ++i)
			{
				for (const FString& Pat : Patterns[i])
				{
					if (Pat.Contains(TEXT("*")) == (Pass == 1) && V.Name.MatchesWildcard(Pat, ESearchCase::IgnoreCase))
					{
						Target = i;
						break;
					}
				}
			}
		}
		Out[Target == INDEX_NONE ? Out.Num() - 1 : Target].Stats.Add(V);
	}
	Out.RemoveAll([](const FMRStatSection& S) { return S.Stats.Num() == 0; });
}

bool UMRUISubsystem::GetVital(int32 Index, float& OutValue, float& OutMax) const
{
	const UMRNetSubsystem* Net = GetNet();
	const FMRNetStatGroup* G = Net && Net->GetPhase() == EMRNetPhase::InGame ? Net->FindStatGroup(1) : nullptr;
	if (G && G->bReceived && G->Stats.IsValidIndex(Index))
	{
		// Server 104's group 1, Condition: health, mana, vigor, experience (the bar fills to the current maximum)
		const FMRNetStat& S = G->Stats[Index];
		OutValue = static_cast<float>(S.Value);
		OutMax = static_cast<float>(S.CurrentMax > 0 ? S.CurrentMax : S.Max);
		return true;
	}
	const UMRAttributeSet* A = GetAttributes();
	if (!A)
	{
		OutValue = OutMax = 0.f;
		return false;
	}
	switch (Index)
	{
	case 0: OutValue = A->GetHealth(); OutMax = A->GetMaxHealth(); break;
	case 1: OutValue = A->GetMana(); OutMax = A->GetMaxMana(); break;
	default: OutValue = A->GetVigor(); OutMax = A->GetMaxVigor(); break;
	}
	return true;
}

// ------------------------------------------------------------------------------ avatar and map

void UMRUISubsystem::EnsureAvatar()
{
	APlayerController* PC = GetPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (!Avatar)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Avatar = World->SpawnActor<AMRAvatarPreview>(AMRAvatarPreview::StaticClass(), AMRAvatarPreview::Location(), FRotator::ZeroRotator, Params);
	}
	if (Avatar)
	{
		if (const AMRCharacter* Char = Cast<AMRCharacter>(PC->GetPawn()))
		{
			Avatar->SetAppearance(Char->GetSpriteAppearance());
		}
	}
}

AMRAvatarPreview* UMRUISubsystem::SpawnPreview(const FVector& Offset)
{
	APlayerController* PC = GetPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	return World->SpawnActor<AMRAvatarPreview>(AMRAvatarPreview::StaticClass(), AMRAvatarPreview::Location() + Offset, FRotator::ZeroRotator, Params);
}

void UMRUISubsystem::EnsureCreatorPreviews()
{
	// (apart from the inventory's avatar and from each other: each capture shows only its own body)
	const FLinearColor Bg = GetStyle() ? GetStyle()->Color(TEXT("creator_preview_bg"), FLinearColor(0.33f, 0.33f, 0.33f)) : FLinearColor(0.33f, 0.33f, 0.33f);
	if (!CreatorPortrait)
	{
		CreatorPortrait = SpawnPreview(FVector(0.0, 4000.0, 0.0));
		if (CreatorPortrait)
		{
			CreatorPortrait->SetPortrait(true);
			CreatorPortrait->SetBackdrop(Bg);
		}
	}
}

void UMRUISubsystem::SetCreatorAppearance(const FMRSpriteAppearance& Appearance)
{
	EnsureCreatorPreviews();
	for (AMRAvatarPreview* P : {CreatorBody.Get(), CreatorPortrait.Get()})
	{
		if (P)
		{
			P->SetAppearance(Appearance);
		}
	}
}

void UMRUISubsystem::SetCreatorCapturing(bool bCapture)
{
	if (bCapture)
	{
		EnsureCreatorPreviews();
	}
	for (AMRAvatarPreview* P : {CreatorBody.Get(), CreatorPortrait.Get()})
	{
		if (P)
		{
			P->SetCapturing(bCapture);
		}
	}
}

UObject* UMRUISubsystem::GetCreatorTarget(bool bPortrait) const
{
	const AMRAvatarPreview* P = bPortrait ? CreatorPortrait.Get() : CreatorBody.Get();
	return P ? P->GetTarget() : nullptr;
}

void UMRUISubsystem::TurnCreatorBody(int32 Steps)
{
	if (CreatorBody)
	{
		CreatorBody->Turn(Steps);
	}
}

TSharedPtr<SMRCharCreator> UMRUISubsystem::GetCreator() const
{
	return Login.IsValid() ? Login->GetCreator() : nullptr;
}

void UMRUISubsystem::TurnCreatorPortrait(int32 Steps)
{
	if (CreatorPortrait)
	{
		CreatorPortrait->Turn(Steps);
	}
}

UObject* UMRUISubsystem::GetAvatarTarget() const
{
	return Avatar ? Avatar->GetTarget() : nullptr;
}

void UMRUISubsystem::TurnAvatar(int32 Steps)
{
	if (Avatar)
	{
		Avatar->Turn(Steps);
	}
}

bool UMRUISubsystem::GetSlotCentre(const FMRSlotRef& Slot, FVector2f& OutAbsolute) const
{
	if (const FVector2f* Found = SlotCentres.Find(SlotKey(Slot)))
	{
		OutAbsolute = *Found;
		return true;
	}
	return false;
}

UTexture2D* UMRUISubsystem::GetMapTexture(int32 GeometryRid)
{
	if (const TObjectPtr<UTexture2D>* Found = MapTextures.Find(GeometryRid))
	{
		return *Found;
	}
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *MRMinimap::TexturePath(GeometryRid), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMRUIStyle::FinishTexture(Tex);
	MapTextures.Add(GeometryRid, Tex);
	return Tex;
}

#undef LOCTEXT_NAMESPACE
