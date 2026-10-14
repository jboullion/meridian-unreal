#include "Ambient/MRAmbientFigure.h"

#include "Character/MRSpriteBodyComponent.h"
#include "Misc/App.h"
#include "Net/MRNetWorld.h"
#include "World/MRBgfSpriteComponent.h"

namespace
{
	/** The sprite body puts the feet this far under its owner (UMRSpriteBodyComponent::PlaceQuad, no capsule) */
	constexpr float FeetBelowCm = 90.f;
}

AMRAmbientFigure::AMRAmbientFigure()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SetCanBeDamaged(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
	Tags.Add(TEXT("MRAmbient"));
}

void AMRAmbientFigure::EnsureBody()
{
	if (!Body && FApp::CanEverRender())
	{
		Body = NewObject<UMRSpriteBodyComponent>(this, TEXT("SpriteBody"));
		Body->SetupAttachment(Root);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->RegisterComponent();
	}
}

void AMRAmbientFigure::SetPerson(const FMRSpriteAppearance& A)
{
	EnsureBody();
	if (Body)
	{
		if (!A.Look.IsNone())
		{
			Body->SetLook(A.Look);
		}
		Body->SetAppearance(A);
		HeightCm = Body->GetStandingHeightCm();
	}
}

void AMRAmbientFigure::SetMonster(FName Look, float Scale)
{
	EnsureBody();
	if (Body)
	{
		Body->SetLook(Look);
		Body->SetHeightScale(Scale);
		HeightCm = Body->GetStandingHeightCm() * Scale;
	}
}

void AMRAmbientFigure::SetBitmap(TSharedPtr<const FMRBgf> Bgf, const FMRNetAnimation& Standing, const FMRNetAnimation& Moving, float Scale)
{
	if (!FApp::CanEverRender())
	{
		return;
	}
	if (!Bitmap)
	{
		Bitmap = NewObject<UMRBgfSpriteComponent>(this, TEXT("Bitmap"));
		Bitmap->SetupAttachment(Root);
		Bitmap->SetRelativeLocation(FVector(0.0, 0.0, -FeetBelowCm));
		Bitmap->RegisterComponent();
	}
	Bitmap->SetRelativeScale3D(FVector(Scale));
	Bitmap->SetLitByWorld(true);
	Bitmap->SetBgf(Bgf);
	Bitmap->SetAnimation(Standing, Moving);
	HeightCm = 60.f * Scale;
}

void AMRAmbientFigure::Place(const FVector& InFeet, float Yaw, const FVector& Velocity)
{
	Feet = InFeet;
	SetActorLocationAndRotation(InFeet + FVector(0.0, 0.0, FeetBelowCm), FRotator(0.f, Yaw, 0.f));
	// the sprite body walks by its owner's velocity (AActor::GetVelocity: the root's)
	Root->ComponentVelocity = Velocity;
	if (Bitmap)
	{
		Bitmap->SetMoving(!Velocity.IsNearlyZero(1.0));
	}
}

void AMRAmbientFigure::PlayAction(FName Action)
{
	if (!Body)
	{
		return;
	}
	if (Action.IsNone())
	{
		Body->StopAction();
	}
	else
	{
		Body->PlayAction(Action);
	}
}

void AMRAmbientFigure::SetFade(float Fade)
{
	const float Want = FMath::Clamp(Fade, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Want, Opacity, 0.01f))
	{
		return;
	}
	Opacity = Want;
	if (Body)
	{
		Body->SetOpacity(Opacity);
	}
	if (Bitmap)
	{
		// proto.h DRAWFX_TRANSLUCENT75/50/25 while fading, as Shards
		static const uint8 Fx[4] = {1, 2, 3, 0};
		Bitmap->SetDrawEffect(Fx[FMath::Min(3, FMath::FloorToInt(Opacity * 4.f))]);
	}
	SetActorHiddenInGame(Opacity <= 0.01f);
}

FVector AMRAmbientFigure::GetSpeechAnchor() const
{
	return Feet + FVector(0.0, 0.0, HeightCm + 25.0);
}
