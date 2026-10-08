#include "Monsters/MRMonster.h"

#include "AIController.h"
#include "Audio/MRAudioSubsystem.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "MeridianRemastered.h"
#include "Net/UnrealNetwork.h"
#include "Character/MRCharacterMovementComponent.h"

namespace
{
	TAutoConsoleVariable<float> CVarCorpseSeconds(TEXT("mr.Monster.CorpseSeconds"), 20.f,
		TEXT("How long a dead monster's corpse stays."));
	TAutoConsoleVariable<float> CVarSpeedScale(TEXT("mr.Monster.SpeedScale"), 1.f,
		TEXT("Monster speed relative to the original's (viSpeed in the remaster's units: a bunny walks about 1 m/s)."));
	TAutoConsoleVariable<float> CVarWander(TEXT("mr.Monster.WanderRadius"), 600.f,
		TEXT("How far (cm) monsters wander from where they spawned."));
	TAutoConsoleVariable<float> CVarLeash(TEXT("mr.Monster.Leash"), 2500.f,
		TEXT("How far (cm) from its spawn point a monster chases before it gives up."));
	constexpr int32 PlaceholderHitsToKill = 3;
	const FName AttackName(TEXT("attack"));
	const FName AwareName(TEXT("aware"));
	const FName DieName(TEXT("die"));
}

AMRMonster::AMRMonster(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCapsuleComponent()->InitCapsuleSize(30.f, 50.f);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetVisibility(false);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 360.f, 0.f);
		Move->MaxWalkSpeed = 100.f;
	}
	SetNetCullDistanceSquared(FMath::Square(30000.f));  // as players: one zone and its neighbours
}

const FMRMonsterDef* AMRMonster::Def() const
{
	return FMRSpriteLibrary::Get().Monsters.Find(MonsterClass);
}

bool AMRMonster::IsNpc() const
{
	const FMRMonsterDef* D = Def();
	return D && D->bNpc;
}

void AMRMonster::InitMonster(FName InClass, int32 InZone)
{
	MonsterClass = InClass;
	Zone = InZone;
	const FMRMonsterDef* D = Def();
	if (!D)
	{
		UE_LOG(LogMeridian, Warning, TEXT("Monster class %s not in data/sprites/player_parts.json"), *InClass.ToString());
		return;
	}
	Look = D->Look;
	// speed_cms is viSpeed in a 4.5 m/s run's units (tools/sprites/monsters.py); scale it to the player's run
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(40.f, D->SpeedCms * UMRCharacterMovementComponent::RunCms() / 450.f
		* CVarSpeedScale.GetValueOnGameThread());
	ApplyLook();
}

void AMRMonster::BeginPlay()
{
	Super::BeginPlay();
	Home = GetActorLocation();
	ApplyLook();
}

void AMRMonster::ApplyLook()
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	// the capsule from the living look (the corpse keeps it, so it lies on the same floor)
	const FMRMonsterDef* D = Def();
	if (const FMRSpriteLook* L = D ? Lib.Looks.Find(D->Look) : nullptr)
	{
		const FMRSpritePart* Body = L->Find(TEXT("body"));
		const FMRSpriteBgf* Bgf = Body ? Lib.FindBgf(Body->Bgf) : nullptr;
		const float CmPerPx = Lib.CmPerBasePixel(Bgf ? Bgf->Shrink : 4);
		// the bounds include the widest attack frames: the body is about a third of that across
		const float Radius = FMath::Clamp(L->Bounds.GetSize().X * CmPerPx * 0.3f, 15.f, 90.f);
		const float Half = FMath::Clamp(-L->Bounds.Min.Y * CmPerPx * 0.5f, FMath::Max(Radius, 20.f), 120.f);
		if (!FMath::IsNearlyEqual(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Half))
		{
			GetCapsuleComponent()->SetCapsuleSize(Radius, Half);
		}
	}
	if (Look.IsNone() || IsNetMode(NM_DedicatedServer) || !FApp::CanEverRender())
	{
		return;  // nothing is drawn on a server
	}
	if (!SpriteBody)
	{
		SpriteBody = NewObject<UMRSpriteBodyComponent>(this, TEXT("SpriteBody"));
		SpriteBody->SetupAttachment(GetCapsuleComponent());
		SpriteBody->RegisterComponent();
	}
	if (SpriteBody->GetLook() != Look)
	{
		SpriteBody->SetLook(Look);
		if (D && Look != D->DeadLook && !D->DeadLook.IsNone())
		{
			SpriteBody->PrewarmLook(D->DeadLook);
		}
	}
}

void AMRMonster::OnRep_Look()
{
	ApplyLook();
}

void AMRMonster::PlayMonsterAction(FName InAction)
{
	Action.Action = InAction;
	Action.Seq++;
	Action.ServerTime = GetWorld()->GetTimeSeconds();
	if (!IsNetMode(NM_DedicatedServer))
	{
		OnRep_Action();  // a listen server or standalone game draws it too
	}
}

void AMRMonster::OnRep_Action()
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (GS && GS->GetServerWorldTimeSeconds() - Action.ServerTime > 3.f)
	{
		return;  // a late joiner: long over
	}
	if (Action.Action == AwareName)
	{
		PlaySound(TEXT("aware"));
		return;
	}
	if (Action.Action == DieName)
	{
		PlaySound(TEXT("death"));
		return;
	}
	if (SpriteBody && !Action.Action.IsNone())
	{
		SpriteBody->PlayAction(Action.Action);
	}
	if (Action.Action == AttackName)
	{
		PlaySound(TEXT("hit"));
	}
}

void AMRMonster::PlaySound(FName Which) const
{
	const FMRMonsterDef* D = Def();
	const FString* File = D ? D->Sounds.Find(Which) : nullptr;
	if (File && !IsNetMode(NM_DedicatedServer))
	{
		UMRAudioSubsystem::Play(this, *File, GetActorLocation());
	}
}

void AMRMonster::TakePlaceholderHit(AActor* From)
{
	if (bDead || IsNpc())
	{
		return;
	}
	bProvoked = true;  // it fights back, aggressive or not
	if (APawn* P = Cast<APawn>(From))
	{
		Target = P;
		State = EState::Chase;
	}
	if (++Hits >= PlaceholderHitsToKill)
	{
		Die();
	}
}

void AMRMonster::Die()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	State = EState::Idle;
	Target = nullptr;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	const FMRMonsterDef* D = Def();
	if (D && !D->DeadLook.IsNone())
	{
		Look = D->DeadLook;
		ApplyLook();
	}
	PlayMonsterAction(DieName);
	SetLifeSpan(FMath::Max(1.f, CVarCorpseSeconds.GetValueOnGameThread()));
}

APawn* AMRMonster::FindTarget() const
{
	const FMRMonsterDef* D = Def();
	const float Vision = D ? D->VisionCm : 2200.f;
	APawn* Best = nullptr;
	float BestDist = Vision;
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.5f);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!P)
		{
			continue;
		}
		const float Dist = FVector::Dist(P->GetActorLocation(), GetActorLocation());
		if (Dist >= BestDist)
		{
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRMonsterSight), false, this);
		Params.AddIgnoredActor(P);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, P->GetActorLocation(), ECC_Visibility, Params))
		{
			continue;  // a wall in the way
		}
		Best = P;
		BestDist = Dist;
	}
	return Best;
}

void AMRMonster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bAI && !bDead)
	{
		ServerThink(DeltaSeconds);
	}
}

void AMRMonster::ServerThink(float DeltaSeconds)
{
	const FMRMonsterDef* D = Def();
	if (!D || D->bNpc || D->bStationary)
	{
		return;
	}
	StateTime += DeltaSeconds;
	AttackCooldown -= DeltaSeconds;
	ThinkTimer -= DeltaSeconds;
	const FVector Here = GetActorLocation();
	if (ThinkTimer <= 0.f)
	{
		ThinkTimer = 0.3f;
		if (D->bAggressive || bProvoked)
		{
			APawn* T = State == EState::Chase && Target.IsValid() ? Target.Get() : FindTarget();
			if (T && State != EState::Chase)
			{
				State = EState::Chase;
				StateTime = 0.f;
				Target = T;
				PlayMonsterAction(AwareName);
			}
		}
	}

	switch (State)
	{
	case EState::Idle:
		if (StateTime > 2.f + FMath::FRand() * 4.f)
		{
			const float R = CVarWander.GetValueOnGameThread();
			WanderTo = Home + FVector(FMath::FRandRange(-R, R), FMath::FRandRange(-R, R), 0.f);
			State = EState::Wander;
			StateTime = 0.f;
			StuckTime = 0.f;
		}
		break;
	case EState::Wander:
	{
		const FVector To = FVector(WanderTo.X - Here.X, WanderTo.Y - Here.Y, 0.f);
		StuckTime = GetVelocity().Size2D() < 10.f ? StuckTime + DeltaSeconds : 0.f;
		if (To.Size() < 60.f || StateTime > 10.f || StuckTime > 1.5f)
		{
			State = EState::Idle;
			StateTime = 0.f;
		}
		else
		{
			AddMovementInput(To.GetSafeNormal(), 0.7f);
		}
		break;
	}
	case EState::Chase:
	{
		APawn* T = Target.Get();
		if (!T || FVector::Dist(Here, Home) > CVarLeash.GetValueOnGameThread() || FVector::Dist(Here, T->GetActorLocation()) > D->VisionCm * 1.5f)
		{
			Target = nullptr;
			bProvoked = false;
			State = EState::Idle;
			StateTime = 0.f;
			break;
		}
		const FVector To = FVector(T->GetActorLocation().X - Here.X, T->GetActorLocation().Y - Here.Y, 0.f);
		const float Reach = GetCapsuleComponent()->GetScaledCapsuleRadius() + 34.f + 80.f;
		if (To.Size() > Reach)
		{
			AddMovementInput(To.GetSafeNormal(), 1.f);
		}
		else
		{
			SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
			if (AttackCooldown <= 0.f)
			{
				const FMRSpriteAction* A = FMRSpriteLibrary::Get().FindAction(Look, AttackName);
				AttackCooldown = FMath::Max(1.5f, (A ? A->OnceLengthMs() : 0) / 1000.f + 0.6f);
				if (A)
				{
					PlayMonsterAction(AttackName);
				}
			}
		}
		break;
	}
	}
}

void AMRMonster::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMRMonster, Look);
	DOREPLIFETIME(AMRMonster, MonsterClass);
	DOREPLIFETIME(AMRMonster, Action);
	DOREPLIFETIME(AMRMonster, bDead);
}
