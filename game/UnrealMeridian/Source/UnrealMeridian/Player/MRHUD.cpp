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
#include "Core/MRUnits.h"

namespace
{
	TAutoConsoleVariable<float> CVarHandScale(TEXT("mr.Sprite.HandScale"), 1.f,
		TEXT("First-person hand / weapon size relative to the original client's (viewport width / 452 * 0.5)."));
	TAutoConsoleVariable<float> CVarHandBob(TEXT("mr.Sprite.HandBob"), 1.f,
		TEXT("First-person hand bob while walking (0 off, as the original)."));
	constexpr float ClassicWidth = 452.f;  // clientd3d/drawdefs.h CLASSIC_WIDTH

	/**
	 * The colour a screen translation flashes or tints the view with (d3drender.c's XLAT_BLEND*
	 * table; xlat.h ids). Anything else is black, as there.
	 */
	FLinearColor XlatTint(uint32 Xlat)
	{
		const auto A = [](int32 Alpha) { return Alpha / 255.f; };
		if (Xlat >= 0x41 && Xlat <= 0x4A)
		{
			return FLinearColor(1.f, 0.f, 0.f, Xlat == 0x4A ? 1.f : A(25 * int32(Xlat - 0x40)));  // XLAT_BLEND10RED..100
		}
		if (Xlat >= 0x70 && Xlat <= 0x79)
		{
			return FLinearColor(1.f, 1.f, 1.f, Xlat == 0x79 ? 1.f : A(25 * int32(Xlat - 0x6F)));  // XLAT_BLEND10WHITE..100
		}
		switch (Xlat)
		{
		case 0x51: return FLinearColor(1.f, 0.f, 0.f, A(75));    // XLAT_BLEND25RED
		case 0x57: return FLinearColor(1.f, 0.f, 0.f, A(200));   // XLAT_BLEND75RED
		case 0x52: return FLinearColor(0.f, 0.f, 1.f, A(64));    // XLAT_BLEND25BLUE
		case 0x55: return FLinearColor(0.f, 0.f, 1.f, A(128));
		case 0x58: return FLinearColor(0.f, 0.f, 1.f, A(192));
		case 0x53: return FLinearColor(0.f, 1.f, 0.f, A(64));    // XLAT_BLEND25GREEN
		case 0x56: return FLinearColor(0.f, 1.f, 0.f, A(128));
		case 0x59: return FLinearColor(0.f, 1.f, 0.f, A(192));
		case 0x39: return FLinearColor(1.f, 1.f, 0.f, 0.25f);    // XLAT_BLEND25YELLOW (the software client's)
		default: return FLinearColor(0.f, 0.f, 0.f, 1.f);
		}
	}
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
		// over both: the loading screen until shaders and the rest are ready (UMRWarmup)
		if (UMRUISubsystem* UI = PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>())
		{
			UI->ShowWarmup(PC);
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
	AMRCharacter* Character = Cast<AMRCharacter>(GetOwningPawn());
	UMRSpriteBodyComponent* Sprite = Character ? Character->GetSpriteBody() : nullptr;
	if (!Canvas || !Sprite)
	{
		return;
	}
	DrawScreenEffects(Character, true);
	if (Character->IsFirstPerson())
	{
		DrawHands(Character, Sprite);
	}
	DrawScreenEffects(Character, false);
}

void AMRHUD::DrawHands(AMRCharacter* Character, UMRSpriteBodyComponent* Sprite)
{
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
			if (P.Hotspot == 0 || T.Track.Group <= 0)
			{
				continue;
			}
			const FString Bgf = FPaths::GetBaseFilename(P.Object.Icon).ToLower();
			if (Sprite->GetBgfFrame(Bgf, T.Track.Group - 1, Tex, UV, Size, Offset) && Tex->GetResource())
			{
				DrawFirstPerson(Tex, UV, Size, Offset, P.Hotspot, Bob, Scale);
			}
		}
		return;  // the swing too is the server's (BP_PLAYER_OVERLAY, docs/adr/0012 M4)
	}
	// offline: the hand or weapon, and the local swing
	if (Sprite->GetFirstPersonFrame(Tex, UV, Size, Offset) && Tex->GetResource())
	{
		DrawFirstPerson(Tex, UV, Size, Offset, MRMsg::HS_SE, Bob, Scale);
	}
}

void AMRHUD::DrawScreenEffects(AMRCharacter* Character, bool bBeforeHands)
{
	const UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	const UMRNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (!Net || !NetWorld || !NetWorld->IsActive())
	{
		return;
	}
	const FMRNetEffects& E = Net->GetEffects();
	const auto Fill = [this](const FLinearColor& Color)
	{
		if (Color.A > 0.f)
		{
			FCanvasTileItem Tile(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), Color);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
	};
	if (bBeforeHands)
	{
		// on the view: clientd3d effect.c EffectShake (a random step of up to a quarter square each
		// frame), the waver's sway, the blur swelling and shrinking (draw3d.c: 1..6 pixels, 150 ms a step)
		FVector Eye = FVector::ZeroVector;
		if (E.ShakeMs > 0.f)
		{
			const double Amp = (FMath::Min(256.0, E.ShakeMs / 3.0) + 1.0) * MRUnits::CmPerSquare / 1024.0;
			Eye = FVector(FMath::FRandRange(-0.5, 0.5), FMath::FRandRange(-0.5, 0.5), FMath::FRandRange(-0.5, 0.5)) * Amp;
		}
		WaverPhase = E.WaverMs > 0.f ? WaverPhase + GetWorld()->GetDeltaSeconds() : 0.0;
		const float Roll = E.WaverMs > 0.f ? 4.f * FMath::Sin(WaverPhase * 2.2) : 0.f;
		float Blur = 0.f;
		if (E.BlurMs > 0.f)
		{
			const int32 Step = static_cast<int32>(E.BlurMs / 150.f) % 12;
			Blur = static_cast<float>((Step > 6 ? 12 - Step : Step) + 1);
		}
		Character->SetViewEffects(Eye, Roll, Blur, E.InvertMs > 0.f);
		// blind: nothing of the world is drawn (d3drender.c), the hands still are
		if (E.bBlind)
		{
			Fill(FLinearColor::Black);
		}
		return;
	}
	// over it, the original's order: a colour flash, the override, the whiteout, then pain last
	if (E.FlashXlat && E.FlashMs > 0.f)
	{
		Fill(XlatTint(E.FlashXlat));
	}
	if (E.XlatOverride)
	{
		Fill(XlatTint(E.XlatOverride));
	}
	if (E.WhiteoutMs > 0.f)
	{
		Fill(FLinearColor(1.f, 1.f, 1.f, FMath::Max(200.f, FMath::Min(E.WhiteoutMs, 500.f) * 255.f / 500.f) / 255.f));
	}
	if (E.PainMs > 0.f)
	{
		Fill(FLinearColor(1.f, 0.f, 0.f, FMath::Min(E.PainMs, 2000.f) * 204.f / 2000.f / 255.f));
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
