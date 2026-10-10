#include "World/MRBgfSpriteComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealMeridian.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "World/MRRuntimeRoom.h"

namespace
{
	/** The server's animation record -> a track over 1-based groups (server.c ExtractAnimation). */
	FMRSpriteTrackDef ToTrack(const FMRNetAnimation& A)
	{
		FMRSpriteTrackDef D;
		switch (A.Type)
		{
		case MRMsg::ANIMATE_CYCLE:
			D.Mode = FMRSpriteTrackDef::EMode::Cycle;
			D.PeriodMs = static_cast<int32>(A.Period);
			D.Low = A.GroupLow;
			D.High = A.GroupHigh;
			D.Final = A.GroupLow;
			break;
		case MRMsg::ANIMATE_ONCE:
			D.Mode = FMRSpriteTrackDef::EMode::Once;
			D.PeriodMs = static_cast<int32>(A.Period);
			D.Low = A.GroupLow;
			D.High = A.GroupHigh;
			D.Final = A.GroupFinal;
			break;
		default:
			D.Low = D.High = D.Final = FMath::Max<int32>(1, A.Group);
			break;
		}
		return D;
	}
}

UMRBgfSpriteComponent::UMRBgfSpriteComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = true;
}

void UMRBgfSpriteComponent::SetBgf(TSharedPtr<const FMRBgf> InBgf)
{
	Bgf = InBgf;
	Textures.Reset();
	Shown = INDEX_NONE;
	if (!Material)
	{
		if (UMaterialInterface* Base = AMRRuntimeRoom::LoadMaterial())
		{
			Material = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	Restart();
}

void UMRBgfSpriteComponent::SetBaseHidden(bool bHidden)
{
	bBaseHidden = bHidden;
	if (bBaseHidden)
	{
		ClearAllMeshSections();
	}
	else if (Bgf && Bgf->Bitmaps.IsValidIndex(Shown))
	{
		Show(Shown);
	}
}

void UMRBgfSpriteComponent::SetOverlays(const TArray<FOverlay>& InOverlays)
{
	for (UMRBgfSpriteComponent* C : OverlayComps)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	OverlayComps.Reset();
	OverlayHotspots.Reset();
	for (const FOverlay& O : InOverlays)
	{
		if (!O.Bgf || O.Bgf->Bitmaps.Num() == 0)
		{
			continue;
		}
		UMRBgfSpriteComponent* C = NewObject<UMRBgfSpriteComponent>(GetOwner());
		C->bIsOverlay = true;
		C->SetupAttachment(this);
		C->RegisterComponent();
		C->SetBgf(O.Bgf);
		C->SetAnimation(O.Animation, O.Animation);
		OverlayComps.Add(C);
		OverlayHotspots.Add(O.Hotspot);
	}
	PlaceOverlays();
}

void UMRBgfSpriteComponent::PlaceOverlays()
{
	if (!Bgf || !Bgf->Bitmaps.IsValidIndex(Shown))
	{
		return;
	}
	const FMRBgf::FBitmap& B = Bgf->Bitmaps[Shown];
	for (int32 i = 0; i < OverlayComps.Num(); ++i)
	{
		UMRBgfSpriteComponent* C = OverlayComps[i];
		const FMRBgf::FHotspot* H = B.Hotspots.FindByPredicate([Num = OverlayHotspots[i]](const FMRBgf::FHotspot& S) { return FMath::Abs(S.Num) == Num; });
		if (!C)
		{
			continue;
		}
		C->SetVisibility(H != nullptr);
		if (!H)
		{
			continue;  // (a hotspot on another overlay: not drawn yet)
		}
		// the overlay's own offsets count in the base's pixels (d3drender.c "add overlay offsets")
		const FMRBgf::FBitmap* OvB = C->Bgf && C->Bgf->Bitmaps.IsValidIndex(C->Shown) ? &C->Bgf->Bitmaps[C->Shown]
			: C->Bgf && C->Bgf->Bitmaps.Num() > 0 ? &C->Bgf->Bitmaps[0] : nullptr;
		const double Px = H->X + (OvB ? OvB->XOffset : 0), Py = H->Y + (OvB ? OvB->YOffset : 0);
		C->SetRelativeLocation(FVector(H->Num > 0 ? 1.0 : -1.0, -(Px - FeetX) * CmPerPx, (FeetY - Py) * CmPerPx));
	}
}

void UMRBgfSpriteComponent::SetDrawEffect(uint8 Effect)
{
	if (!Material)
	{
		return;
	}
	float Opacity = 1.f;
	switch (Effect)
	{
	case MRMsg::DRAWFX_TRANSLUCENT25: Opacity = 0.25f; break;
	case MRMsg::DRAWFX_TRANSLUCENT50: case MRMsg::DRAWFX_DITHERINVIS: case MRMsg::DRAWFX_DITHERGREY: Opacity = 0.5f; break;
	case MRMsg::DRAWFX_TRANSLUCENT75: Opacity = 0.75f; break;
	default: break;
	}
	Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	Material->SetScalarParameterValue(TEXT("Brightness"), Effect == MRMsg::DRAWFX_BLACK ? 0.f : 1.f);
}

void UMRBgfSpriteComponent::SetAnimation(const FMRNetAnimation& Standing, const FMRNetAnimation& Moving)
{
	StandingDef = ToTrack(Standing);
	bHasMovingDef = Moving.Type != 0;
	MovingDef = bHasMovingDef ? ToTrack(Moving) : StandingDef;
	Restart();
}

void UMRBgfSpriteComponent::SetMoving(bool bInMoving)
{
	if (bMoving != bInMoving)
	{
		bMoving = bInMoving;
		if (bHasMovingDef)
		{
			Restart();
		}
	}
}

void UMRBgfSpriteComponent::Restart()
{
	Track.Start(bMoving ? MovingDef : StandingDef);
}

UTexture2D* UMRBgfSpriteComponent::TextureOf(int32 Bitmap)
{
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Bitmap))
	{
		return *Found;
	}
	UTexture2D* Tex = Bgf ? Bgf->MakeTexture(Bitmap, false) : nullptr;
	if (Tex)
	{
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		Tex->UpdateResource();
		Textures.Add(Bitmap, Tex);
	}
	return Tex;
}

void UMRBgfSpriteComponent::Show(int32 Bitmap)
{
	Shown = Bitmap;
	const FMRBgf::FBitmap& B = Bgf->Bitmaps[Bitmap];
	// one pixel is 16/shrink Kod fine units; the feet as the original finds them (FMRSpriteLibrary::Place);
	// an overlay hangs from its top left corner (its parent places it: PlaceOverlays)
	CmPerPx = 16.0 / Bgf->Shrink / 1024.0 * 220.0;
	FeetX = bIsOverlay ? 0.0 : B.Width * 0.5 - B.XOffset * Bgf->Shrink / 16.0;
	FeetY = bIsOverlay ? 0.0 : B.Height - B.YOffset * Bgf->Shrink / 4.0;
	PlaceOverlays();
	if (bBaseHidden)
	{
		ClearAllMeshSections();
		return;
	}
	// the quad faces +X (the viewer); the viewer's right is -Y
	auto Corner = [&](double Px, double Py) { return FVector(0.0, -(Px - FeetX) * CmPerPx, (FeetY - Py) * CmPerPx); };
	const TArray<FVector> Pos = {Corner(0, 0), Corner(B.Width, 0), Corner(B.Width, B.Height), Corner(0, B.Height)};
	const TArray<FVector2D> UV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
	const TArray<FVector> Normals = {FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector};
	// alpha 0: M_RuntimeRoom leaves out the room's light model (it's for the room's surfaces)
	const FLinearColor Full(1.f, 1.f, 1.f, 0.f);
	const TArray<FLinearColor> Colors = {Full, Full, Full, Full};
	CreateMeshSection_LinearColor(0, Pos, {0, 2, 1, 0, 3, 2}, Normals, UV, Colors, TArray<FProcMeshTangent>(), false);
	if (Material)
	{
		Material->SetTextureParameterValue(TEXT("Tex"), TextureOf(Bitmap));
		SetMaterial(0, Material);
	}
	UE_LOG(LogMeridian, Verbose, TEXT("World: %s shows bitmap %d (%dx%d, shrink %d, %.0f cm tall) at %s, material %s"), *GetNameSafe(GetOwner()),
		Bitmap, B.Width, B.Height, Bgf->Shrink, B.Height * CmPerPx, *GetComponentLocation().ToString(), *GetNameSafe(Material));
}

void UMRBgfSpriteComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(MRBgfSpriteTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Bgf || Bgf->Bitmaps.Num() == 0 || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	Track.Step(DeltaTime * 1000.f);
	const FVector Here = GetComponentLocation();
	const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
	const double YawToViewer = FMath::RadiansToDegrees(FMath::Atan2(Camera.Y - Here.Y, Camera.X - Here.X));
	// upright and turned to the camera, as the original drew objects (an overlay turns with its parent)
	if (!bIsOverlay)
	{
		SetWorldRotation(FRotator(0.0, YawToViewer, 0.0));
	}

	int32 Bitmap = 0;
	const int32 Group = Track.Group - 1;  // server groups are 1-based (server.h BitmapGroupSToC)
	if (Bgf->Groups.IsValidIndex(Group) && Bgf->Groups[Group].Num() > 0)
	{
		const TArray<int32>& G = Bgf->Groups[Group];
		const float Facing = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.f;
		const int32 Angle = FMRSpriteLibrary::RelativeAngle(Facing, static_cast<float>(YawToViewer));
		Bitmap = G[FMRSpriteLibrary::ViewSlot(Angle, G.Num())];
	}
	if (Bgf->Bitmaps.IsValidIndex(Bitmap) && Bitmap != Shown)
	{
		Show(Bitmap);
	}
}
