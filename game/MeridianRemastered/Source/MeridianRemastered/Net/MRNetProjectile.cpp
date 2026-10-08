#include "Net/MRNetProjectile.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "World/MRBgfSpriteComponent.h"
#include "Zones/MRZoneSubsystem.h"

AMRNetProjectile::AMRNetProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);  // local only: the Meridian server is the authority
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMRNetProjectile::Launch(const FVector& InFrom, const FVector& InTo, double SpeedCms, bool bInFollowGround)
{
	From = InFrom;
	To = InTo;
	bFollowGround = bInFollowGround;
	Elapsed = 0.0;
	// project.c: speed 0, or nowhere to go, arrives at once
	const double Dist = FVector::Dist(From, To);
	Duration = SpeedCms > 0.0 && Dist > 1.0 ? Dist / SpeedCms : 0.0;
	const FVector Dir = (To - From).GetSafeNormal2D();
	SetActorLocationAndRotation(From, Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation());
	if (Duration <= 0.0)
	{
		SetLifeSpan(0.05f);
	}
}

void AMRNetProjectile::SetBgf(TSharedPtr<const FMRBgf> Bgf, const FMRNetAnimation& Animation, int32 DrawEffect)
{
	if (!Sprite)
	{
		Sprite = NewObject<UMRBgfSpriteComponent>(this, TEXT("Sprite"));
		Sprite->SetupAttachment(RootComponent);
		Sprite->RegisterComponent();
	}
	Sprite->SetBgf(Bgf);
	Sprite->SetAnimation(Animation, FMRNetAnimation());
	if (DrawEffect >= 0)
	{
		Sprite->SetDrawEffect(static_cast<uint8>(DrawEffect));
	}
}

void AMRNetProjectile::SetLight(const FMRNetLight& L)
{
	if (L.Flags == MRMsg::LIGHT_FLAG_NONE || L.Intensity == 0)
	{
		return;
	}
	if (!Light)
	{
		Light = NewObject<UPointLightComponent>(this, TEXT("Light"));
		Light->SetupAttachment(RootComponent);
		Light->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
		Light->SetCastShadows(false);
		Light->RegisterComponent();
	}
	// 5:5:5 colour; the original's intensity is how far it reaches (d_lighting)
	const auto Channel = [&L](int32 Shift) { return static_cast<uint8>(((L.Color >> Shift) & 31) * 255 / 31); };
	Light->SetLightColor(FColor(Channel(10), Channel(5), Channel(0)));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(4.f + L.Intensity / 16.f);
	Light->SetAttenuationRadius(200.f + L.Intensity * 6.f);
}

void AMRNetProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Duration <= 0.0)
	{
		return;
	}
	Elapsed += DeltaSeconds;
	if (Elapsed >= Duration)
	{
		Destroy();
		return;
	}
	FVector At = FMath::Lerp(From, To, Elapsed / Duration);
	if (bFollowGround)
	{
		// project.c: PROJ_FLAG_FOLLOWGROUND keeps it on the floor under it
		FVector Floor = At;
		if (UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>(); Zones && Zones->TraceFloor(Floor))
		{
			At.Z = Floor.Z;
		}
	}
	SetActorLocation(At);
}
