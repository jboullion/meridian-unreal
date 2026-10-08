#include "UI/MRAvatarPreview.h"

#include "Character/MRSpriteBodyComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianRemastered.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
	constexpr float CameraDistance = 400.f;
	constexpr float OrthoWidthCm = 230.f;  // a little more than a tall character
	constexpr int32 TargetSize = 512;
	// the portrait: the head (with the tallest hair) fills it; the face's centre above the feet, as
	// a share of the standing height (the original's 1.84 m male, 1.70 m female)
	constexpr float PortraitWidthCm = 44.f;
	constexpr float PortraitFaceHeight = 0.925f;      // female (phkx)
	constexpr float PortraitFaceHeightMale = 0.912f;  // phax has a longer neck: centre on the head
	constexpr float PortraitTexelsPerPixel = 9.f;  // render target texels per torso pixel: ~2.6 per face pixel
}

AMRAvatarPreview::AMRAvatarPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// the camera in front of the character (it faces +X), level with its middle
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	// fixed in the world: turning the actor (the character's facing) must not move the camera
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = OrthoWidthCm;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetEyeAdaptation(false);
	Capture->ShowFlags.SetLensFlares(false);
	Capture->ShowFlags.SetAmbientOcclusion(false);
	// the sprite is unlit: show its colours as they are (no auto exposure)
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.f;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.f;
}

void AMRAvatarPreview::BeginPlay()
{
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this, TEXT("AvatarTarget"));
	Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(TargetSize, TargetSize);
	Target->UpdateResourceImmediate(true);
	Capture->TextureTarget = Target;

	Body = NewObject<UMRSpriteBodyComponent>(this, TEXT("AvatarBody"));
	Body->SetupAttachment(Root);
	Body->SetForceUnlit(true);
	Body->SetViewer(Capture);
	Body->SetCastShadow(false);
	Body->RegisterComponent();
	Capture->ShowOnlyComponents.Add(Body);
	PlaceCamera();
}

void AMRAvatarPreview::SetPortrait(bool bInPortrait)
{
	bPortrait = bInPortrait;
	Capture->OrthoWidth = bPortrait ? PortraitWidthCm : OrthoWidthCm;
	if (Body)
	{
		Body->SetDensityOverride(bPortrait ? PortraitTexelsPerPixel : 0.f);
		// the original creator drew the head with its face parts and hair, nothing else (charface.c)
		Body->SetOnlyParts(bPortrait ? TArray<FName>{TEXT("head"), TEXT("eyes"), TEXT("nose"), TEXT("mouth"), TEXT("hair")} : TArray<FName>());
	}
	PlaceCamera();
}

void AMRAvatarPreview::SetBackdrop(const FLinearColor& Colour)
{
	if (!Backdrop)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/Sprites/M_PreviewBackdrop.M_PreviewBackdrop"));
		if (!Plane || !Base)
		{
			UE_LOG(LogMeridian, Warning, TEXT("Preview backdrop: M_PreviewBackdrop missing (run tools/ue/import_sprites.ps1)"));
			return;
		}
		Backdrop = NewObject<UStaticMeshComponent>(this, TEXT("Backdrop"));
		Backdrop->SetStaticMesh(Plane);
		Backdrop->SetMaterial(0, UMaterialInstanceDynamic::Create(Base, this));
		Backdrop->SetCastShadow(false);
		Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backdrop->SetupAttachment(Root);
		// behind the character, facing the camera (a plane's normal is +Z), wider than any view; fixed
		// in the world like the camera, so turning the character doesn't swing it in front of it
		Backdrop->SetUsingAbsoluteLocation(true);
		Backdrop->SetUsingAbsoluteRotation(true);
		Backdrop->SetWorldLocation(GetActorLocation() - FVector(150.f, 0.f, 0.f));
		Backdrop->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
		Backdrop->SetWorldScale3D(FVector(8.f));
		Backdrop->RegisterComponent();
		Capture->ShowOnlyComponents.Add(Backdrop);
	}
	if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Backdrop->GetMaterial(0)))
	{
		M->SetVectorParameterValue(TEXT("Color"), Colour);
	}
}

void AMRAvatarPreview::PlaceCamera()
{
	// in front of the character (it faces +X): level with its middle, or with its face
	FVector At = GetActorLocation() + FVector(CameraDistance, 0.f, 0.f);
	if (bPortrait && Body)
	{
		const float Feet = GetActorLocation().Z - 90.f;  // (UMRSpriteBodyComponent::PlaceQuad: no capsule)
		const float Height = Body->GetStandingHeightCm();
		const bool bFemale = Body->GetLook().ToString().Contains(TEXT("female"));
		At.Z = Feet + (bFemale ? 170.f * PortraitFaceHeight : 184.f * PortraitFaceHeightMale) * Body->GetHeightScale();
		(void)Height;
	}
	Capture->SetWorldLocationAndRotation(At, FRotator(0.f, 180.f, 0.f));
}

void AMRAvatarPreview::SetAppearance(const FMRSpriteAppearance& A)
{
	if (!Body)
	{
		return;
	}
	if (bHasAppearance && A == Shown)
	{
		return;
	}
	Body->SetAppearance(A);
	Body->SetHeightScale(A.HeightPct > 0 ? A.HeightPct / 100.f : 1.f);
	Shown = A;
	PlaceCamera();
	bHasAppearance = true;
}

void AMRAvatarPreview::SetCapturing(bool bCapture)
{
	Capture->bCaptureEveryFrame = bCapture;
	SetActorTickEnabled(bCapture);
	if (Body)
	{
		Body->SetComponentTickEnabled(bCapture);
	}
}

void AMRAvatarPreview::Turn(int32 Steps)
{
	// the original draws eight angles: turning the character by 45 degrees shows the next one
	AddActorWorldRotation(FRotator(0.f, -45.f * Steps, 0.f));
}
