#include "Player/MRHUD.h"

#include "CanvasItem.h"
#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
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

	const float Scale = Canvas->ClipX / ClassicWidth * 0.5f * CVarHandScale.GetValueOnGameThread();
	const FVector2D Bob(FMath::Cos(BobPhase) * 6.f * Scale * BobAmount, FMath::Abs(FMath::Sin(BobPhase)) * 8.f * Scale * BobAmount);
	UTexture2D* Tex = nullptr;
	FBox2f UV;
	FIntPoint Size, Offset;
	// the local swing, while it plays (online, ahead of the server's; offline the only one)
	const bool bLocal = Sprite->IsFirstPersonAttacking();
	const UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	const UMRNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (NetWorld && NetWorld->IsActive() && Net)
	{
		// online: what the server says is held (clientd3d overlay.c DrawPlayerOverlays)
		const FMRNetWorld& W = Net->GetNetWorld();
		for (auto It = SlotTracks.CreateIterator(); It; ++It)
		{
			if (!W.PlayerOverlays.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
		for (const TPair<uint32, FMRNetPlayerOverlay>& Pair : W.PlayerOverlays)
		{
			const FMRNetPlayerOverlay& P = Pair.Value;
			FSlotTrack& T = SlotTracks.FindOrAdd(Pair.Key);
			if (T.Seq != P.Seq)
			{
				const FMRNetAnimation& A = P.Object.Animation;
				FMRSpriteTrackDef Def;
				Def.Mode = A.Type == MRMsg::ANIMATE_CYCLE ? FMRSpriteTrackDef::EMode::Cycle
					: A.Type == MRMsg::ANIMATE_ONCE ? FMRSpriteTrackDef::EMode::Once : FMRSpriteTrackDef::EMode::None;
				Def.PeriodMs = static_cast<int32>(A.Period);
				Def.Low = A.Type == MRMsg::ANIMATE_NONE ? A.Group : A.GroupLow;
				Def.High = A.Type == MRMsg::ANIMATE_NONE ? A.Group : A.GroupHigh;
				Def.Final = A.Type == MRMsg::ANIMATE_ONCE ? A.GroupFinal : Def.Low;
				T.Track.Start(Def);
				T.Seq = P.Seq;
			}
			else
			{
				T.Track.Step(Dt * 1000.f);
			}
			if (P.Hotspot == 0 || T.Track.Group <= 0 || (bLocal && Pair.Key == MRMsg::PWO_RIGHT_HAND))
			{
				continue;
			}
			const FString Bgf = FPaths::GetBaseFilename(P.Object.Icon).ToLower();
			if (Sprite->GetBgfFrame(Bgf, T.Track.Group - 1, Tex, UV, Size, Offset) && Tex->GetResource())
			{
				DrawFirstPerson(Tex, UV, Size, Offset, P.Hotspot, Bob, Scale);
			}
		}
		if (!bLocal)
		{
			return;
		}
	}
	if (Sprite->GetFirstPersonFrame(Tex, UV, Size, Offset) && Tex->GetResource())
	{
		DrawFirstPerson(Tex, UV, Size, Offset, MRMsg::HS_SE, Bob, Scale);
	}
}

void AMRHUD::DrawFirstPerson(UTexture2D* Tex, const FBox2f& UV, FIntPoint Size, FIntPoint Offset, int32 Hotspot, const FVector2D& Bob, float Scale)
{
	// clientd3d overlay.c ComputePlayerOverlayArea: a corner, an edge's middle or the centre
	const FVector2D DrawSize(Size.X * Scale, Size.Y * Scale);
	const int32 Col = (Hotspot == 1 || Hotspot == 8 || Hotspot == 7) ? 0 : (Hotspot == 3 || Hotspot == 4 || Hotspot == 5) ? 2 : 1;
	const int32 Row = (Hotspot == 1 || Hotspot == 2 || Hotspot == 3) ? 0 : (Hotspot == 7 || Hotspot == 6 || Hotspot == 5) ? 2 : 1;
	const double X = Col == 0 ? 0.0 : Col == 2 ? Canvas->ClipX - DrawSize.X : (Canvas->ClipX - DrawSize.X) * 0.5;
	const double Y = Row == 0 ? 0.0 : Row == 2 ? Canvas->ClipY - DrawSize.Y : (Canvas->ClipY - DrawSize.Y) * 0.5;
	const FVector2D Pos = FVector2D(X + Offset.X * Scale, Y + Offset.Y * Scale) + Bob;
	FCanvasTileItem Item(Pos, Tex->GetResource(), DrawSize, FVector2D(UV.Min.X, UV.Min.Y), FVector2D(UV.Max.X, UV.Max.Y), FLinearColor::White);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}
