#include "UI/MRUISubsystem.h"

#include "Abilities/MRAttributeSet.h"
#include "Character/MRCharacter.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Player/MRPlayerState.h"
#include "UI/MRAvatarPreview.h"
#include "UI/MRGameData.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/SMRHUD.h"
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

	UMRMockInventory* Mock = NewObject<UMRMockInventory>(this);
	Mock->SetData(GetData());
	Mock->LoadFromJson();
	Mock->OnSelectionChanged.AddWeakLambda(this, [this]() { SelectionTime = Now(); });
	Source = Mock;

	if (UMRUIStyle* Style = GetStyle())
	{
		StyleHandle = Style->OnReloaded.AddUObject(this, &UMRUISubsystem::OnStyleReloaded);
	}
}

void UMRUISubsystem::Deinitialize()
{
	RemoveHUD();
	if (UMRUIStyle* Style = GetStyle())
	{
		Style->OnReloaded.Remove(StyleHandle);
	}
	Super::Deinitialize();
}

UMRUIStyle* UMRUISubsystem::GetStyle() const
{
	const UGameInstance* GI = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRUIStyle>() : nullptr;
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

void UMRUISubsystem::SetInventoryOpen(bool bOpen)
{
	if (!HUD.IsValid() || bOpen == bInventoryOpen)
	{
		return;
	}
	bInventoryOpen = bOpen;
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

void UMRUISubsystem::ApplyInputMode()
{
	APlayerController* PC = GetPlayerController();
	if (!PC)
	{
		return;
	}
	if (bInventoryOpen)
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
	return Item ? Style->Icon(Item->Icon) : nullptr;
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
	const FMRItemDef* Item = Data->FindItem(C.Id);
	return Item ? Item->Name : FText::FromName(C.Id);
}

TSharedPtr<IToolTip> UMRUISubsystem::MakeToolTip(const FMRSlotContent& C) const
{
	UMRUIStyle* S = GetStyle();
	UMRGameDataSubsystem* Data = GetData();
	if (!S || !Data)
	{
		return nullptr;
	}
	const float Px = S->Px();
	const FLinearColor Title = S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f));
	const FLinearColor Body(0.82f, 0.8f, 0.74f);
	const FLinearColor Detail = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	FText Desc, Line;
	if (C.bSpell)
	{
		if (const FMRSpellDef* Spell = Data->FindSpell(C.Id))
		{
			Desc = Spell->Desc;
			Line = FText::Format(LOCTEXT("TipSpell", "{0}, level {1}  ·  {2} mana"), UMRGameDataSubsystem::SchoolName(Spell->School),
				FText::AsNumber(Spell->Level), FText::AsNumber(Spell->Mana));
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
	auto Text = [S, Px](const FText& T, float Size, bool bBold, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(T).Font(S->Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(true)
			.ShadowOffset(FVector2D(1.0, 1.0) * Px * 0.5).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	};
	return SNew(SToolTip)
		.BorderImage(S->Brush(TEXT("invbkgnd"), true))
		.TextMargin(FMargin(5.f * Px))
		[
			SNew(SBox).MaxDesiredWidth(150.f * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(NameFor(C), 11.f, true, Title)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[Text(Line, 8.f, false, Detail)]
				+ SVerticalBox::Slot().AutoHeight()[Text(Desc, 8.5f, false, Body)]
			]
		];
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
