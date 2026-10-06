#include "Environment/MRFireActor.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "Environment/MRFireSubsystem.h"

AMRFireActor::AMRFireActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	Flame = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("Flame"));
	Flame->SetupAttachment(Root);
	Flame->SetMobility(EComponentMobility::Static);
	Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flame->SetCastShadow(false);
	Flame->bAffectDynamicIndirectLighting = false;
	Flame->SetVisibility(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetUseTemperature(false);
	Light->SetCastShadows(false);
	Light->SetVisibility(false);
}

void AMRFireActor::SetFlame(UMaterialInterface* Material, float WidthCm, float HeightCm)
{
	TArray<FMaterialSpriteElement> Elements;
	if (Material)
	{
		FMaterialSpriteElement Element;
		Element.Material = Material;
		Element.bSizeIsInScreenSpace = false;
		// the billboard proxy spans +-BaseSizeX along the view's up axis and +-BaseSizeY along its right
		Element.BaseSizeX = HeightCm * 0.5f;
		Element.BaseSizeY = WidthCm * 0.5f;
		Elements.Add(Element);
	}
	Flame->SetElements(Elements);
	Flame->SetVisibility(Material != nullptr);
}

void AMRFireActor::SetLight(float Candela, float RadiusCm, FLinearColor Color, FVector OffsetCm, float SourceRadiusCm,
	bool bInFlicker, bool bInShadowEligible, int32 InSeed)
{
	BaseIntensity = FMath::Max(0.f, Candela);
	BaseColor = Color;
	BaseLightOffset = OffsetCm;
	bFlicker = bInFlicker;
	bShadowEligible = bInShadowEligible;
	Seed = InSeed;
	Light->SetRelativeLocation(OffsetCm);
	Light->SetIntensity(BaseIntensity);
	Light->SetAttenuationRadius(RadiusCm);
	Light->SetLightColor(Color);
	Light->SetSourceRadius(SourceRadiusCm);
	Light->SetCastShadows(false);  // UMRFireSubsystem hands out shadows in game
	Light->SetVisibility(BaseIntensity > 0.f);
}

void AMRFireActor::BeginPlay()
{
	Super::BeginPlay();
	if (UMRFireSubsystem* Fires = GetWorld()->GetSubsystem<UMRFireSubsystem>())
	{
		Fires->Register(this);
	}
}

void AMRFireActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMRFireSubsystem* Fires = GetWorld() ? GetWorld()->GetSubsystem<UMRFireSubsystem>() : nullptr)
	{
		Fires->Unregister(this);
	}
	Super::EndPlay(Reason);
}
