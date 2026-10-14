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
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "World/MRRuntimeRooms.h"

namespace
{
	TAutoConsoleVariable<float> CVarHandScale(TEXT("mr.Sprite.HandScale"), 1.f,
		TEXT("First-person hand / weapon size relative to the original D3D client's (and Meridian Shards'): a bitmap pixel 1.75/800 of the view's 4:3 width across, 2.25/600 of its height down."));
	TAutoConsoleVariable<float> CVarHandBob(TEXT("mr.Sprite.HandBob"), 1.f,
		TEXT("First-person hand bob while walking (0 off, as the original)."));
	TAutoConsoleVariable<int32> CVarHandsLit(TEXT("mr.Sprite.HandsLit"), 1,
		TEXT("1: the first-person hands are quads just in front of the camera, lit by the world like the other sprites (M_BgfSpriteLit), ")
		TEXT("as the original and Meridian Shards light them by the player's light; 0: drawn on the screen at the bitmaps' full brightness, as until 2026-10-14."));
	TAutoConsoleVariable<float> CVarHandDistance(TEXT("mr.Sprite.HandDistance"), 15.f,
		TEXT("How far in front of the eye the lit first-person hands hang, cm: past the near clip plane (10), inside the capsule so no wall comes between."));
	TAutoConsoleVariable<float> CVarHandBrightness(TEXT("mr.Sprite.HandBrightness"), 0.45f,
		TEXT("The lit first-person hands' colour multiplier (M_BgfSpriteLit Brightness, on linear light: 0.45 shows as about 0.7). Lit as a sprite ")
		TEXT("beside the player, the hand read brighter than Meridian Shards' (the Inn's bar: 63 against 45); 0.45 matches it (2026-10-14)."));
	const TCHAR* HandMaterialPath = TEXT("/Game/Generated/Runtime/M_BgfSpriteLit.M_BgfSpriteLit");
	/**
	 * d3drender.c D3DComputePlayerOverlayArea: the D3D client draws into an 800 x 600 back buffer stretched
	 * over the view, an overlay pixel 1.75 back-buffer pixels wide and 2.25 tall. As Meridian Shards does
	 * (screenOverlays.ts), measured on the largest 4:3 area that fits the view, so the hands keep their
	 * 800 x 600 shape in a wide window. (Until 2026-10-14: the view's width / CLASSIC_WIDTH 452 * 0.5 both
	 * ways, about 1.5 times narrower and 1.9 times shorter than the original's.)
	 */
	constexpr float OverlayPixelX = 1.75f / 800.f;
	constexpr float OverlayPixelY = 2.25f / 600.f;

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
		HideWorldHands();
		return;
	}
	DrawScreenEffects(Character, true);
	if (Character->IsFirstPerson())
	{
		DrawHands(Character, Sprite);
	}
	else
	{
		HideWorldHands();
	}
	DrawScreenEffects(Character, false);
}

void AMRHUD::PostRender()
{
	Super::PostRender();
	if (!bShowHUD)
	{
		HideWorldHands();  // the photo (AMRCharacter::TakePhoto) has no hands
	}
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

	const float W43 = FMath::Min(Canvas->ClipX, Canvas->ClipY * 4.f / 3.f);
	const FVector2D Scale = FVector2D(OverlayPixelX * W43, OverlayPixelY * W43 * 0.75f) * CVarHandScale.GetValueOnGameThread();
	const FVector2D Bob(FMath::Cos(BobPhase) * 6.f * Scale.X * BobAmount, FMath::Abs(FMath::Sin(BobPhase)) * 8.f * Scale.Y * BobAmount);
	UTexture2D* Tex = nullptr;
	FBox2f UV;
	FIntPoint Size, Offset;
	TArray<FHand> Hands;
	bool bLit = CVarHandsLit.GetValueOnGameThread() != 0;
	const UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	const UMRNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (NetWorld && NetWorld->IsActive() && Net)
	{
		// online: what the server says is held (clientd3d overlay.c DrawPlayerOverlays)
		const FMRNetWorld& W = Net->GetNetWorld();
		// a room built at runtime keeps the original's light model, its sprites at their bitmaps' brightness;
		// blind draws no world, only the hands (d3drender.c): on the screen in both
		bLit = bLit && NetWorld->GetRid() > 0 && NetWorld->GetRid() < UMRRuntimeRooms::RuntimeRidBase && !Net->GetEffects().bBlind;
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
				Hands.Add(PlaceFirstPerson(Tex, UV, Size, Offset, P.Hotspot, Bob, Scale));
			}
		}
		// the swing too is the server's (BP_PLAYER_OVERLAY, docs/adr/0012 M4)
	}
	// offline: the hand or weapon, and the local swing
	else if (Sprite->GetFirstPersonFrame(Tex, UV, Size, Offset) && Tex->GetResource())
	{
		Hands.Add(PlaceFirstPerson(Tex, UV, Size, Offset, MRMsg::HS_SE, Bob, Scale));
	}
	if (bLit && ShowWorldHands(Character, Hands))
	{
		return;
	}
	HideWorldHands();
	for (const FHand& H : Hands)
	{
		FCanvasTileItem Item(H.Pos, H.Tex->GetResource(), H.Size, FVector2D(H.UV.Min.X, H.UV.Min.Y), FVector2D(H.UV.Max.X, H.UV.Max.Y), FLinearColor::White);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

bool AMRHUD::ShowWorldHands(AMRCharacter* Character, const TArray<FHand>& Hands)
{
	UCameraComponent* Camera = Character->GetCamera();
	static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, HandMaterialPath);
	if (!Camera || !Base || Canvas->ClipX <= 0.f || Canvas->ClipY <= 0.f)
	{
		return false;
	}
	// a screen pixel -> the camera's frame, on a plane Depth in front of the eye (the horizontal field of
	// view across the view's width, as the local player keeps it)
	const APlayerController* PC = GetOwningPlayerController();
	const float Fov = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : Camera->FieldOfView;
	const double TanX = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5));
	const double W = Canvas->ClipX, H = Canvas->ClipY;
	const auto Local = [W, H, TanX](double X, double Y, double Depth)
	{
		return FVector(Depth, (2.0 * X / W - 1.0) * Depth * TanX, (1.0 - 2.0 * Y / H) * Depth * TanX * H / W);
	};
	// the character's own components (the HUD actor is hidden in game, and so would they be): new ones for a new pawn
	if (HandsOwner.Get() != Character)
	{
		for (UProceduralMeshComponent* Old : HandQuads)
		{
			if (IsValid(Old))
			{
				Old->DestroyComponent();
			}
		}
		HandQuads.Reset();
		HandMaterials.Reset();
		HandsOwner = Character;
	}
	for (int32 i = 0; i < Hands.Num(); i++)
	{
		const FHand& Hand = Hands[i];
		if (!HandQuads.IsValidIndex(i))
		{
			UProceduralMeshComponent* NewQuad = NewObject<UProceduralMeshComponent>(Character);
			NewQuad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewQuad->SetGenerateOverlapEvents(false);
			NewQuad->SetCastShadow(false);
			NewQuad->bAffectDistanceFieldLighting = false;
			NewQuad->bAffectDynamicIndirectLighting = false;
			NewQuad->bReceivesDecals = false;
			NewQuad->bUseAsOccluder = false;
			NewQuad->SetVisibleInRayTracing(false);
			NewQuad->RegisterComponent();
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
			NewQuad->SetMaterial(0, Material);
			HandQuads.Add(NewQuad);
			HandMaterials.Add(Material);
		}
		UProceduralMeshComponent* Quad = HandQuads[i];
		if (Quad->GetAttachParent() != Camera)
		{
			Quad->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		// each later one a little nearer, over the earlier as the canvas drew them
		const double Depth = CVarHandDistance.GetValueOnGameThread() * (1.0 - 0.02 * i);
		const FVector2D& P = Hand.Pos;
		const FVector2D& S = Hand.Size;
		const TArray<FVector> Pos = {Local(P.X, P.Y, Depth), Local(P.X + S.X, P.Y, Depth), Local(P.X + S.X, P.Y + S.Y, Depth), Local(P.X, P.Y + S.Y, Depth)};
		const TArray<FVector2D> UV = {FVector2D(Hand.UV.Min.X, Hand.UV.Min.Y), FVector2D(Hand.UV.Max.X, Hand.UV.Min.Y),
			FVector2D(Hand.UV.Max.X, Hand.UV.Max.Y), FVector2D(Hand.UV.Min.X, Hand.UV.Max.Y)};
		const TArray<FVector> Normals = {FVector(-1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0)};
		const TArray<FLinearColor> Colors = {FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White};
		if (Quad->GetNumSections() == 0)
		{
			Quad->CreateMeshSection_LinearColor(0, Pos, {0, 1, 2, 0, 2, 3}, Normals, UV, Colors, TArray<FProcMeshTangent>(), false);
		}
		else
		{
			Quad->UpdateMeshSection_LinearColor(0, Pos, Normals, UV, Colors, TArray<FProcMeshTangent>());
		}
		HandMaterials[i]->SetTextureParameterValue(TEXT("Tex"), Hand.Tex);
		HandMaterials[i]->SetScalarParameterValue(TEXT("Brightness"), CVarHandBrightness.GetValueOnGameThread());
		Quad->SetVisibility(true);
	}
	HideWorldHands(Hands.Num());
	return true;
}

void AMRHUD::HideWorldHands(int32 First)
{
	for (int32 i = First; i < HandQuads.Num(); i++)
	{
		if (IsValid(HandQuads[i]) && HandQuads[i]->IsVisible())
		{
			HandQuads[i]->SetVisibility(false);
		}
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

AMRHUD::FHand AMRHUD::PlaceFirstPerson(UTexture2D* Tex, const FBox2f& UV, FIntPoint Size, FIntPoint Offset, int32 Hotspot, const FVector2D& Bob, const FVector2D& Scale) const
{
	// clientd3d overlay.c ComputePlayerOverlayArea: a corner, an edge's middle or the centre
	const FVector2D DrawSize(Size.X * Scale.X, Size.Y * Scale.Y);
	const int32 Col = (Hotspot == 1 || Hotspot == 8 || Hotspot == 7) ? 0 : (Hotspot == 3 || Hotspot == 4 || Hotspot == 5) ? 2 : 1;
	const int32 Row = (Hotspot == 1 || Hotspot == 2 || Hotspot == 3) ? 0 : (Hotspot == 7 || Hotspot == 6 || Hotspot == 5) ? 2 : 1;
	const double X = Col == 0 ? 0.0 : Col == 2 ? Canvas->ClipX - DrawSize.X : (Canvas->ClipX - DrawSize.X) * 0.5;
	const double Y = Row == 0 ? 0.0 : Row == 2 ? Canvas->ClipY - DrawSize.Y : (Canvas->ClipY - DrawSize.Y) * 0.5;
	FHand Hand;
	Hand.Tex = Tex;
	Hand.UV = UV;
	Hand.Pos = FVector2D(X + Offset.X * Scale.X, Y + Offset.Y * Scale.Y) + Bob;
	Hand.Size = DrawSize;
	return Hand;
}
