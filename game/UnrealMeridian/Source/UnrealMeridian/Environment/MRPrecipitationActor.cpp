#include "Environment/MRPrecipitationActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UnrealMeridian.h"

namespace
{
	const TCHAR* MeshPath = TEXT("/Game/Generated/Environment/Kit/SM_Precip/SM_Precip/StaticMeshes/SM_Precip.SM_Precip");

	UStaticMeshComponent* MakeQuads(AActor* Owner, const TCHAR* Name)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetGenerateOverlapEvents(false);
		// the camera is up to a box away from the actor's grid cell, and the quads are moved on the GPU
		C->SetBoundsScale(4.f);
		C->SetHiddenInGame(true);
		return C;
	}
}

AMRPrecipitationActor::AMRPrecipitationActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;  // after the camera has moved
	bReplicates = false;
	SetCanBeDamaged(false);

	Mesh = MakeQuads(this, TEXT("Mesh"));
	RootComponent = Mesh;
	Splash = MakeQuads(this, TEXT("Splash"));
	Splash->SetupAttachment(Mesh);
	Ambient = MakeQuads(this, TEXT("Ambient"));
	Ambient->SetupAttachment(Mesh);

	Bolt = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("Bolt"));
	Bolt->SetupAttachment(Mesh);
	Bolt->SetUsingAbsoluteLocation(true);  // placed in the sky, not with the grid
	Bolt->SetCastShadow(false);
	Bolt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bolt->SetHiddenInGame(true);
}

void AMRPrecipitationActor::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Quads = LoadObject<UStaticMesh>(nullptr, MeshPath))
	{
		Mesh->SetStaticMesh(Quads);
		Splash->SetStaticMesh(Quads);
		Ambient->SetStaticMesh(Quads);
	}
	else
	{
		UE_LOG(LogMeridian, Warning, TEXT("Precipitation: no %s (tools/blender/build_prop_kit.py, then build_world.ps1)"), MeshPath);
	}
}

void AMRPrecipitationActor::SetMaterials(UMaterialInterface* Falling, UMaterialInterface* SplashMaterial)
{
	if (Mesh->GetMaterial(0) != Falling)
	{
		Mesh->SetMaterial(0, Falling);
	}
	if (Splash->GetMaterial(0) != SplashMaterial)
	{
		Splash->SetMaterial(0, SplashMaterial);
	}
	SetFalling(bWantFalling, bWantSplashing);
}

void AMRPrecipitationActor::SetFalling(bool bFalling, bool bSplashing)
{
	bWantFalling = bFalling;
	bWantSplashing = bSplashing;
	const bool bHasMesh = Mesh->GetStaticMesh() != nullptr;
	const bool bShowFall = bFalling && bHasMesh && Mesh->GetMaterial(0) != nullptr;
	const bool bShowSplash = bSplashing && bHasMesh && Splash->GetMaterial(0) != nullptr;
	if (Mesh->bHiddenInGame == bShowFall)
	{
		Mesh->SetHiddenInGame(!bShowFall);
	}
	if (Splash->bHiddenInGame == bShowSplash)
	{
		Splash->SetHiddenInGame(!bShowSplash);
	}
}

void AMRPrecipitationActor::SetAmbient(UMaterialInterface* Material, bool bShow)
{
	if (Material && Ambient->GetMaterial(0) != Material)
	{
		Ambient->SetMaterial(0, Material);
	}
	bWantAmbient = bShow;
	const bool bVisible = bShow && Ambient->GetStaticMesh() != nullptr && Ambient->GetMaterial(0) != nullptr;
	if (Ambient->bHiddenInGame == bVisible)
	{
		Ambient->SetHiddenInGame(!bVisible);
	}
}

void AMRPrecipitationActor::ShowBolt(UMaterialInterface* Material, const FVector& Location, float WidthCm, float HeightCm)
{
	if (!Material)
	{
		Bolt->SetHiddenInGame(true);
		return;
	}
	TArray<FMaterialSpriteElement> Elements;
	FMaterialSpriteElement Element;
	Element.Material = Material;
	Element.bSizeIsInScreenSpace = false;
	// the billboard proxy spans +-BaseSizeX along the view's up axis and +-BaseSizeY along its right
	Element.BaseSizeX = HeightCm * 0.5f;
	Element.BaseSizeY = WidthCm * 0.5f;
	Elements.Add(Element);
	Bolt->SetElements(Elements);
	Bolt->SetWorldLocation(Location);
	Bolt->SetHiddenInGame(false);
}

void AMRPrecipitationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bWantFalling && !bWantSplashing && !bWantAmbient)
	{
		return;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Snapped(FMath::FloorToDouble(Cam.X / BoxCm) * BoxCm, FMath::FloorToDouble(Cam.Y / BoxCm) * BoxCm,
		FMath::FloorToDouble(Cam.Z / BoxHeightCm) * BoxHeightCm);
	if (!Snapped.Equals(GetActorLocation(), 1.0))
	{
		SetActorLocation(Snapped);
	}
}
