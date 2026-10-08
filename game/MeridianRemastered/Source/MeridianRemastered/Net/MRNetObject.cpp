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
		if (!A.Look.IsNone() && SpriteBody->GetLook() != A.Look)
		{
			Look = A.Look;
			SpriteBody->SetLook(A.Look);
		}
		SpriteBody->SetPartBgfs(A.PartBgfs());
		SpriteBody->SetColours(A.Skin, A.Hair, A.Shirt, A.Pants);
	}
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
	if (FVector::Dist2D(At, GetActorLocation()) > SnapCm)
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
