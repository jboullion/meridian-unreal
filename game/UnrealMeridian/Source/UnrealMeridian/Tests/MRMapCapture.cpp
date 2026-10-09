#include "Tests/MRMapCapture.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Ticker.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "UI/SMRMinimap.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	constexpr float SettleSeconds = 4.f;   // lighting and mood after moving into the zone
	constexpr float CaptureSeconds = 2.5f; // captures every frame this long (Lumen, exposure)
	constexpr double AboveOutdoorsCm = 30000.0;
}

bool UMRMapCapture::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRMapCapture"));
}

void UMRMapCapture::After(float Seconds, void (UMRMapCapture::*Step)())
{
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Step](float)
	{
		(this->*Step)();
		return false;
	}), Seconds);
}

void UMRMapCapture::Start(APlayerController* InController)
{
	Controller = InController;
	OutDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../build/minimap/raw")));
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const UMRZoneSubsystem* ZoneSys = InController->GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	TSet<int32> Geometry;
	if (ZoneSys)
	{
		for (const TPair<int32, FMRZoneInfo>& Z : ZoneSys->GetZones())
		{
			Geometry.Add(Z.Value.GeometryRid);
		}
	}
	FString Only;
	TArray<FString> OnlyList;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRMapCaptureOnly="), Only, false))
	{
		Only.ParseIntoArray(OnlyList, TEXT(","));
	}
	for (int32 Rid : Geometry)
	{
		if ((OnlyList.Num() == 0 || OnlyList.Contains(FString::FromInt(Rid))) && !MRMinimap::Settings(Rid).bSkip)
		{
			Zones.Add(Rid);
		}
	}
	Zones.Sort();
	UE_LOG(LogMeridian, Display, TEXT("MRMapCapture: %d zones"), Zones.Num());
	After(3.f, &UMRMapCapture::NextZone);
}

void UMRMapCapture::NextZone()
{
	APlayerController* PC = Controller.Get();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UMRZoneSubsystem* ZoneSys = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!ZoneSys)
	{
		return;
	}
	if (++Index >= Zones.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRMapCapture: done (%s)"), *OutDir);
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FMRZoneInfo* Zone = ZoneSys->FindZone(Zones[Index]);
	APawn* Pawn = PC->GetPawn();
	if (Zone && Pawn)
	{
		// stand in the zone so its lighting and mood apply; out of the picture
		const FVector Arrival = Zone->Origin + Zone->TeleportLocal + FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() + 2.f);
		Pawn->TeleportTo(Arrival, Pawn->GetActorRotation());
		Pawn->SetActorHiddenInGame(true);
	}
	After(SettleSeconds, &UMRMapCapture::Capture);
}

void UMRMapCapture::Capture()
{
	APlayerController* PC = Controller.Get();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UMRZoneSubsystem* ZoneSys = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	const int32 Rid = Zones[Index];
	const FMRZoneInfo* Zone = ZoneSys ? ZoneSys->FindZone(Rid) : nullptr;
	FBox2D Rect;
	if (!Zone || !MRMinimap::CaptureRect(ZoneSys, Rid, Rect))
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRMapCapture: %d has no footprint; skipped"), Rid);
		After(0.f, &UMRMapCapture::NextZone);
		return;
	}
	const MRMinimap::FZoneSettings Settings = MRMinimap::Settings(Rid);

	if (!Camera)
	{
		Camera = World->SpawnActor<ASceneCapture2D>();
		USceneCaptureComponent2D* C = Camera->GetCaptureComponent2D();
		C->ProjectionType = ECameraProjectionMode::Orthographic;
		// the near plane at the camera (the default for an ortho capture), and the camera left where
		// it is: bUpdateOrthoPlanes would move the view back and bring the ceilings into the picture
		C->bAutoCalculateOrthoPlanes = false;
		C->bUpdateOrthoPlanes = false;
		C->bOverride_CustomNearClippingPlane = true;
		C->CustomNearClippingPlane = 0.f;
		C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		C->bCaptureEveryFrame = false;
		C->bCaptureOnMovement = false;
		C->bAlwaysPersistRenderingState = true;  // exposure and Lumen build up over the frames
		C->ShowFlags.SetFog(false);
		C->ShowFlags.SetVolumetricFog(false);
		C->ShowFlags.SetMotionBlur(false);
		C->ShowFlags.SetLensFlares(false);
		C->ShowFlags.SetBloom(false);
		// nothing but the zone below: no sky (its "does not cover that part of the screen" warning
		// would be drawn over the empty parts of the picture)
		C->ShowFlags.SetAtmosphere(false);
		C->ShowFlags.SetCloud(false);
	}
	USceneCaptureComponent2D* C = Camera->GetCaptureComponent2D();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
	// the lit picture comes out display-ready; the albedo is linear and wants the sRGB target
	Target->RenderTargetFormat = Settings.bBaseColor ? ETextureRenderTargetFormat::RTF_RGBA8_SRGB : ETextureRenderTargetFormat::RTF_RGBA8;
	C->CaptureSource = Settings.bBaseColor ? ESceneCaptureSource::SCS_BaseColor : ESceneCaptureSource::SCS_FinalColorLDR;
	Target->InitAutoFormat(Settings.Resolution, Settings.Resolution);
	Target->UpdateResourceImmediate(true);
	C->TextureTarget = Target;

	// straight down, north (-Y) at the top of the picture, east (+X) to the right
	const double FloorZ = Zone->Origin.Z + Zone->TeleportLocal.Z;
	const bool bInterior = Settings.CutHeightCm > 0.f;
	const double Z = bInterior ? FloorZ + Settings.CutHeightCm : FloorZ + AboveOutdoorsCm;
	const FVector2D Centre = Rect.GetCenter();
	Camera->SetActorLocationAndRotation(FVector(Centre.X, Centre.Y, Z), FRotator(-90.f, -90.f, 0.f));
	C->OrthoWidth = Rect.GetSize().X;
	C->ShowFlags.SetDynamicShadows(Settings.bShadows);
	C->ShowFlags.SetContactShadows(Settings.bShadows);
	C->HiddenActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		// characters and monsters never; props, fires and smoke when the zone asks (interiors)
		const bool bProp = It->ActorHasTag(TEXT("ZoneProp")) || It->ActorHasTag(TEXT("ZoneFire")) || It->ActorHasTag(TEXT("ZoneEffect"));
		if (It->IsA<APawn>() || (Settings.bHideProps && bProp))
		{
			C->HiddenActors.Add(*It);
		}
	}

	C->bCaptureEveryFrame = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Rid, Rect, Target, bInterior, Z](float)
	{
		USceneCaptureComponent2D* Comp = Camera ? Camera->GetCaptureComponent2D() : nullptr;
		if (!Comp)
		{
			return false;
		}
		Comp->bCaptureEveryFrame = false;
		Comp->CaptureScene();
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Res = Target->GameThread_GetRenderTargetResource();
		if (Res && Res->ReadPixels(Pixels) && Pixels.Num() == Target->SizeX * Target->SizeY)
		{
			for (FColor& P : Pixels)
			{
				P.A = 255;
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Base = FPaths::Combine(OutDir, FString::Printf(TEXT("T_Map_%d"), Rid));
			FFileHelper::SaveArrayToFile(Png, *(Base + TEXT(".png")));
			FFileHelper::SaveStringToFile(FString::Printf(
				TEXT("{\"rid\": %d, \"rect_min_cm\": [%.1f, %.1f], \"rect_max_cm\": [%.1f, %.1f], \"interior\": %s, \"camera_z_cm\": %.1f}\n"),
				Rid, Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y, bInterior ? TEXT("true") : TEXT("false"), Z), *(Base + TEXT(".json")));
			UE_LOG(LogMeridian, Display, TEXT("MRMapCapture: %s.png (%.0f m square)"), *Base, Rect.GetSize().X / 100.0);
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRMapCapture: %d: reading the capture failed"), Rid);
		}
		After(0.f, &UMRMapCapture::NextZone);
		return false;
	}), CaptureSeconds);
}
