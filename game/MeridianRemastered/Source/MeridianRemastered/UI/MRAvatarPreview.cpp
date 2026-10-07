#include "UI/MRAvatarPreview.h"

#include "Character/MRSpriteBodyComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
	constexpr float CameraDistance = 400.f;
	constexpr float OrthoWidthCm = 230.f;  // a little more than a tall character
	constexpr int32 TargetSize = 512;
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
	Capture->SetWorldLocationAndRotation(GetActorLocation() + FVector(CameraDistance, 0.f, 0.f), FRotator(0.f, 180.f, 0.f));
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
}

void AMRAvatarPreview::SetAppearance(const FMRSpriteAppearance& A)
{
	if (!Body)
	{
		return;
	}
	if (bHasAppearance && A.Look == Shown.Look && A.Skin == Shown.Skin && A.Hair == Shown.Hair && A.Shirt == Shown.Shirt
		&& A.Pants == Shown.Pants && A.HeightPct == Shown.HeightPct)
	{
		return;
	}
	if (!A.Look.IsNone() && Body->GetLook() != A.Look)
	{
		Body->SetLook(A.Look);
	}
	Body->SetColours(A.Skin, A.Hair, A.Shirt, A.Pants);
	Body->SetHeightScale(A.HeightPct > 0 ? A.HeightPct / 100.f : 1.f);
	Shown = A;
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
