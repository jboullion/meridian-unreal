#include "Player/MRHUD.h"

#include "CanvasItem.h"
#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetWorldSubsystem.h"
#include "UI/MRUISubsystem.h"
#include "Character/MRCharacterMovementComponent.h"

namespace
{
	TAutoConsoleVariable<float> CVarHandScale(TEXT("mr.Sprite.HandScale"), 1.f,
		TEXT("First-person hand / weapon size relative to the original client's (viewport width / 452 * 0.5)."));
	TAutoConsoleVariable<float> CVarHandBob(TEXT("mr.Sprite.HandBob"), 1.f,
		TEXT("First-person hand bob while walking (0 off, as the original)."));
	constexpr float ClassicWidth = 452.f;  // clientd3d/drawdefs.h CLASSIC_WIDTH
}

void AMRHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (PC && PC->IsLocalController() && PC->GetLocalPlayer())
	{
		// online, the login screen comes first (UMRNetWorldSubsystem shows the HUD once in the game)
		if (UMRNetWorldSubsystem* Net = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>(); Net && Net->IsActive())
		{
			Net->UpdateScreens();
		}
		else if (UMRUISubsystem* UI = PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>())
		{
			UI->ShowHUD(PC);
		}
	}
}

void AMRHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	APlayerController* PC = GetOwningPlayerController();
	if (PC && PC->GetLocalPlayer())
	{
		if (UMRUISubsystem* UI = PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>())
		{
			UI->RemoveHUD();
		}
	}
	Super::EndPlay(Reason);
}

void AMRHUD::DrawHUD()
{
	Super::DrawHUD();
	const AMRCharacter* Character = Cast<AMRCharacter>(GetOwningPawn());
	UMRSpriteBodyComponent* Sprite = Character ? Character->GetSpriteBody() : nullptr;
	if (!Canvas || !Sprite || !Character->IsFirstPerson())
	{
		return;
	}

	// walk bob: one step per half cycle, faster when running
	const double Now = GetWorld()->GetTimeSeconds();
	const float Dt = LastTime > 0.0 ? static_cast<float>(Now - LastTime) : 0.f;
	LastTime = Now;
	const float Speed = Character->GetVelocity().Size2D();
	BobPhase += Dt * FMath::Clamp(Speed / UMRCharacterMovementComponent::RunCms(), 0.f, 2.f) * UE_TWO_PI * 1.2f;
	const float BobAmount = FMath::Clamp(Speed / UMRCharacterMovementComponent::WalkCms(), 0.f, 1.f) * CVarHandBob.GetValueOnGameThread();

	UTexture2D* Tex = nullptr;
	FBox2f UV;
	FIntPoint Size, Offset;
	if (!Sprite->GetFirstPersonFrame(Tex, UV, Size, Offset) || !Tex->GetResource())
	{
		return;
	}
	const float Scale = Canvas->ClipX / ClassicWidth * 0.5f * CVarHandScale.GetValueOnGameThread();
	const FVector2D DrawSize(Size.X * Scale, Size.Y * Scale);
	const FVector2D Bob(FMath::Cos(BobPhase) * 6.f * Scale * BobAmount, FMath::Abs(FMath::Sin(BobPhase)) * 8.f * Scale * BobAmount);
	const FVector2D Pos = FVector2D(Canvas->ClipX - DrawSize.X + Offset.X * Scale, Canvas->ClipY - DrawSize.Y + Offset.Y * Scale) + Bob;
	FCanvasTileItem Item(Pos, Tex->GetResource(), DrawSize, FVector2D(UV.Min.X, UV.Min.Y), FVector2D(UV.Max.X, UV.Max.Y), FLinearColor::White);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}
