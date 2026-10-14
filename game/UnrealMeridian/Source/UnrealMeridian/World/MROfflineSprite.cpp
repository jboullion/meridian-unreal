#include "World/MROfflineSprite.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Net/MRNetSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarOfflineSprites(TEXT("mr.World.OfflineSprites"), 1,
		TEXT("Offline: draw the original sprites of the objects without their 3D prop (props.json \"mesh_keep\"). 0 hides them ")
		TEXT("(as zones load; -MRNoOfflineSprites from the start)."));
}

AMROfflineSprite::AMROfflineSprite(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UStaticMeshComponent* Mesh = GetStaticMeshComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetCastShadow(false);  // a quad turned to the camera casts a shadow that turns with it (the online bitmaps cast none)
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetGenerateOverlapEvents(false);
}

void AMROfflineSprite::BeginPlay()
{
	Super::BeginPlay();
	if (UMRNetSubsystem::WantsOnline(GetWorld()) || CVarOfflineSprites.GetValueOnGameThread() == 0
		|| FParse::Param(FCommandLine::Get(), TEXT("MRNoOfflineSprites")))
	{
		SetActorHiddenInGame(true);
	}
}
