#include "Net/MRNetObject.h"

#include "AIController.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/MRUnits.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Character/MRCharacterMovementComponent.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "World/MRBgfSpriteComponent.h"

namespace
{
	/** Farther than this from where the server says it is: jump there (a teleport, a missed update). */
	constexpr double SnapCm = 4.0 * MRUnits::CmPerSquare;
	/** Close enough to stop walking. */
	constexpr double ArriveCm = 12.0;
}

AMRNetObject::AMRNetObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);  // local only: the Meridian server is the authority
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::Spawned;  // a controller so it can walk
	bUseControllerRotationYaw = false;
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(30.f, 88.f);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetVisibility(false);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 540.f, 0.f);
		Move->MaxWalkSpeed = 220.f;
		Move->bRunPhysicsWithNoController = true;
	}
}

void AMRNetObject::Init(uint32 InId, FName InLook, const FString& InName)
{
	ServerId = InId;
	Look = InLook;
	ObjectName = InName;
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Net_%u_%s"), InId, *InName.Replace(TEXT(" "), TEXT("_"))));
#endif
	if (Look.IsNone() || !FApp::CanEverRender())
	{
		return;
	}
	if (!SpriteBody)
	{
		SpriteBody = NewObject<UMRSpriteBodyComponent>(this, TEXT("SpriteBody"));
		SpriteBody->SetupAttachment(GetCapsuleComponent());
		SpriteBody->RegisterComponent();
	}
	SpriteBody->SetLook(Look);
}

void AMRNetObject::SetAppearance(const FMRSpriteAppearance& A)
{
	if (SpriteBody)
	{
		if (!A.Look.IsNone())
		{
			Look = A.Look;
		}
		SpriteBody->SetAppearance(A);
	}
}

void AMRNetObject::SetBgfSprite(const FString& InBgfName, TSharedPtr<const FMRBgf> Bgf)
{
	BgfName = InBgfName;
	if (!FApp::CanEverRender())
	{
		return;
	}
	if (!BgfSprite)
	{
		BgfSprite = NewObject<UMRBgfSpriteComponent>(this, TEXT("BgfSprite"));
		BgfSprite->SetupAttachment(GetCapsuleComponent());
		// the feet on the floor: the capsule's bottom
		BgfSprite->SetRelativeLocation(FVector(0.0, 0.0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
		BgfSprite->RegisterComponent();
	}
	BgfSprite->SetBgf(Bgf);
	SetDrawEffect(DrawEffect);
}

void AMRNetObject::SetServerAnimation(const FMRNetAnimation& Standing, const FMRNetAnimation& Moving)
{
	if (BgfSprite)
	{
		BgfSprite->SetAnimation(Standing, Moving);
	}
}

void AMRNetObject::PlayAction(FName Action)
{
	if (SpriteBody)
	{
		SpriteBody->PlayAction(Action);
	}
}

void AMRNetObject::SetStatic()
{
	bStatic = true;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->DisableMovement();
		Move->SetComponentTickEnabled(false);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void AMRNetObject::SetShownByProp(double InPropTop)
{
	bShownByProp = true;
	PropTop = InPropTop;
}

void AMRNetObject::SetDrawEffect(uint8 Effect)
{
	DrawEffect = Effect;
	const bool bInvisible = Effect == MRMsg::DRAWFX_INVISIBLE;
	if (BgfSprite)
	{
		BgfSprite->SetVisibility(!bInvisible);
		BgfSprite->SetDrawEffect(Effect);
	}
	if (SpriteBody)
	{
		SpriteBody->SetVisibility(!bInvisible, true);
		// as the bitmap sprites (UMRBgfSpriteComponent::SetDrawEffect): a ghost (DRAWFX_DITHERINVIS) half shows
		float Opacity = 1.f;
		switch (Effect)
		{
		case MRMsg::DRAWFX_TRANSLUCENT25: Opacity = 0.25f; break;
		case MRMsg::DRAWFX_TRANSLUCENT50: case MRMsg::DRAWFX_DITHERINVIS: case MRMsg::DRAWFX_DITHERGREY: Opacity = 0.5f; break;
		case MRMsg::DRAWFX_TRANSLUCENT75: Opacity = 0.75f; break;
		default: break;
		}
		SpriteBody->SetOpacity(Opacity);
	}
}

FVector AMRNetObject::GetNameAnchor() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight());
	if (bShownByProp)
	{
		return FVector(Feet.X, Feet.Y, FMath::Max(PropTop, Feet.Z + 30.0) + 25.0);
	}
	if (BgfSprite && BgfSprite->GetNumSections() > 0)
	{
		return FVector(Feet.X, Feet.Y, BgfSprite->Bounds.GetBox().Max.Z + 25.0);
	}
	if (SpriteBody)
	{
		// over its head: a player's 1.84 m, a bunny's knee height (the look's box above the feet)
		const float Height = SpriteBody->GetStandingHeightCm();
		return Feet + FVector(0.0, 0.0, Height > 0.f ? FMath::Min(Height, 200.f) + 15.0 : 215.0);
	}
	return Feet + FVector(0.0, 0.0, 60.0);
}

void AMRNetObject::Place(const FVector& World, int32 KodAngle)
{
	const FVector At = World + FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0);
	StandYaw = MRUnits::KodAngleToYaw(KodAngle);
	SetActorLocationAndRotation(At, FRotator(0.0, StandYaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	Target = At;
	bHasTarget = false;
}

void AMRNetObject::MoveTo(const FVector& World, uint8 Speed)
{
	const FVector At = World + FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0);
	if (bStatic || FVector::Dist2D(At, GetActorLocation()) > SnapCm)
	{
		SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		bHasTarget = false;
		return;
	}
	Target = At;
	bHasTarget = true;
	// the server's speed is in the original's units (a player walks at 25, runs at 55): 55 is the
	// player's run (UMRCharacterMovementComponent::RunCms)
	const float Run = UMRCharacterMovementComponent::RunCms();
	const float Cms = Speed > 0 ? Speed * Run / 55.f : UMRCharacterMovementComponent::WalkCms();
	GetCharacterMovement()->MaxWalkSpeed = FMath::Clamp(Cms, 60.f, 2.f * Run);
}

void AMRNetObject::TurnTo(int32 KodAngle)
{
	StandYaw = MRUnits::KodAngleToYaw(KodAngle);
	if (!bHasTarget)
	{
		SetActorRotation(FRotator(0.0, StandYaw, 0.0));
	}
}

void AMRNetObject::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (BgfSprite)
	{
		BgfSprite->SetMoving(bHasTarget);
	}
	if (!bHasTarget)
	{
		return;
	}
	const FVector To = Target - GetActorLocation();
	const double Dist = To.Size2D();
	// one frame's step past the target means we're there
	if (Dist <= FMath::Max(ArriveCm, GetCharacterMovement()->MaxWalkSpeed * DeltaSeconds))
	{
		bHasTarget = false;
		GetCharacterMovement()->StopMovementImmediately();
		SetActorRotation(FRotator(0.0, StandYaw, 0.0));
		return;
	}
	AddMovementInput(FVector(To.X, To.Y, 0.0).GetSafeNormal(), 1.f);
}
