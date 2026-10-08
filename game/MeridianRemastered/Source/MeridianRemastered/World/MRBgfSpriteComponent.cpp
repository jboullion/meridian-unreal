#include "World/MRBgfSpriteComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianRemastered.h"
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
	// one pixel is 16/shrink Kod fine units; the feet as the original finds them (FMRSpriteLibrary::Place)
	const double CmPerPx = 16.0 / Bgf->Shrink / 1024.0 * 220.0;
	const double FeetX = B.Width * 0.5 - B.XOffset * Bgf->Shrink / 16.0;
	const double FeetY = B.Height - B.YOffset * Bgf->Shrink / 4.0;
	// the quad faces +X (the viewer); the viewer's right is -Y
	auto Corner = [&](double Px, double Py) { return FVector(0.0, -(Px - FeetX) * CmPerPx, (FeetY - Py) * CmPerPx); };
	const TArray<FVector> Pos = {Corner(0, 0), Corner(B.Width, 0), Corner(B.Width, B.Height), Corner(0, B.Height)};
	const TArray<FVector2D> UV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
	const TArray<FVector> Normals = {FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector};
	const TArray<FLinearColor> Colors = {FLinearColor::White, FLinearColor::White, FLinearColor::White, FLinearColor::White};
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
	// upright and turned to the camera, as the original drew objects
	SetWorldRotation(FRotator(0.0, YawToViewer, 0.0));

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
