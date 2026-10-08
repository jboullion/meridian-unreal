#include "Tests/MRMoveTest.h"

#include "Character/MRCharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"

namespace
{
	// the test rig: empty space well away from the zones' 2 km grid
	const FVector Rig(-300000.0, -300000.0, 0.0);
	// a Mausoleum (306) step of 0.722 m, between the original's 0.825 and a modern 0.45 (roo2gltf
	// heights at (5.6, 69.6) m and (3.2, 69.6) m in the zone; its origin is (400000, 200000, 0) cm)
	const FVector CryptLow(400560.0, 206957.5, 288.8);
	const double CryptHighZ = 360.9;
}

bool UMRMoveTest::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRMoveTest"));
}

TStatId UMRMoveTest::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRMoveTest, STATGROUP_Tickables);
}

void UMRMoveTest::Start(APlayerController* InController)
{
	Controller = InController;
	const float Run = UMRCharacterMovementComponent::RunCms();
	const float Walk = UMRCharacterMovementComponent::WalkCms();
	auto SetZone = [this](int32 Rid)
	{
		if (AMRPlayerState* PS = Controller.IsValid() ? Controller->GetPlayerState<AMRPlayerState>() : nullptr)
		{
			PS->SetZoneId(Rid);  // 0: in no zone, so no exit fires on the test rig
		}
	};
	auto SpeedCheck = [this](float Expected)
	{
		return [this, Expected](FString& Out)
		{
			Out = FString::Printf(TEXT("%.0f cm/s (the original's %.0f)"), MeasuredSpeed, Expected);
			return FMath::Abs(MeasuredSpeed - Expected) < Expected * 0.05;
		};
	};
	auto Flat = [this, SetZone]()
	{
		SetZone(0);
		ClearBoxes();
		Box(Rig + FVector(-3000.0, -1000.0, -100.0), Rig + FVector(3000.0, 1000.0, 0.0));
		Place(Rig + FVector(-2500.0, 0.0, 0.0), FVector::ForwardVector);
	};
	// a ledge 2 m above a pit floor, and a platform as high across a gap of GapCm
	auto Ledge = [this, SetZone](double GapCm)
	{
		return [this, SetZone, GapCm]()
		{
			SetZone(0);
			ClearBoxes();
			Box(Rig + FVector(-3000.0, -1000.0, -100.0), Rig + FVector(3000.0, 1000.0, 0.0));
			Box(Rig + FVector(-1500.0, -200.0, 0.0), Rig + FVector(0.0, 200.0, 200.0));
			Box(Rig + FVector(GapCm, -200.0, 0.0), Rig + FVector(GapCm + 1000.0, 200.0, 200.0));
			Place(Rig + FVector(-1200.0, 0.0, 200.0), FVector::ForwardVector);
		};
	};

	Steps = {
		{TEXT("Mausoleum step of 0.72 m: runs up it"),
			[this, SetZone]() { SetZone(306); ClearBoxes(); Place(CryptLow, -FVector::ForwardVector); },
			-FVector::ForwardVector, false, 1.0f,
			[this](FString& Out)
			{
				Out = FString::Printf(TEXT("feet at %.0f cm (low %.0f, high %.0f)"), FeetZ(), CryptLow.Z, CryptHighZ);
				return FeetZ() > CryptHighZ - 20.0;
			}},
		{TEXT("run speed"), Flat, FVector::ForwardVector, false, 2.f, SpeedCheck(Run)},
		{TEXT("walk speed (Shift)"), Flat, FVector::ForwardVector, true, 2.f, SpeedCheck(Walk)},
		{TEXT("running off a ledge across a 2.5 m gap: lands on the far side"), Ledge(250.0), FVector::ForwardVector, false, 1.6f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("ended %.0f cm past the ledge, feet at %.0f cm (the platforms' top: 200)"), P.X - Rig.X, FeetZ());
				return P.X - Rig.X > 300.0 && FeetZ() > 150.0;
			}},
		{TEXT("walking off a ledge across a 2.5 m gap: falls in (too slow)"), Ledge(250.0), FVector::ForwardVector, true, 2.5f,
			[this](FString& Out)
			{
				Out = FString::Printf(TEXT("feet at %.0f cm (pit floor: 0)"), FeetZ());
				return FeetZ() < 50.0;
			}},
		// near the original's limit: at run speed it flies 3.58 m before dropping a step, and stops
		// 53 cm short of a wall (move.c min_distance, half of game.c player.width): about 4.1 m
		{TEXT("running off a ledge across a 3.9 m gap: lands on the far side"), Ledge(390.0), FVector::ForwardVector, false, 1.6f,
			[this](FString& Out)
			{
				Out = FString::Printf(TEXT("feet at %.0f cm (the platforms' top: 200)"), FeetZ());
				return FeetZ() > 150.0;
			}},
		{TEXT("running off a ledge across a 4.5 m gap: falls in"), Ledge(450.0), FVector::ForwardVector, false, 1.6f,
			[this](FString& Out)
			{
				Out = FString::Printf(TEXT("feet at %.0f cm (pit floor: 0)"), FeetZ());
				return FeetZ() < 50.0;
			}},
		{TEXT("running off a ledge across a 5 m gap: falls in"), Ledge(500.0), FVector::ForwardVector, false, 1.6f,
			[this](FString& Out)
			{
				Out = FString::Printf(TEXT("feet at %.0f cm (pit floor: 0)"), FeetZ());
				return FeetZ() < 50.0;
			}},
	};
	UE_LOG(LogMeridian, Display, TEXT("MRMoveTest: starting, %d steps (run %.0f, walk %.0f cm/s, gravity %.0f cm/s/s, step %.1f cm)"),
		Steps.Num(), Run, Walk, UMRCharacterMovementComponent::OriginalGravityCms2, UMRCharacterMovementComponent::OriginalStepCm);
	// let the start zone stream in and the pawn spawn
	FTimerHandle Timer;
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this]() { Begin(0); }), 4.f, false);
}

void UMRMoveTest::Begin(int32 NewIndex)
{
	Index = NewIndex;
	if (!Controller.IsValid() || !Controller->GetPawn())
	{
		UE_LOG(LogMeridian, Error, TEXT("MRMoveTest: no pawn"));
		return;
	}
	if (!Steps.IsValidIndex(Index))
	{
		ClearBoxes();
		UE_LOG(LogMeridian, Display, TEXT("MRMoveTest: DONE %d/%d"), Passed, Steps.Num());
		Controller->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FStep& S = Steps[Index];
	S.Setup();
	if (UMRCharacterMovementComponent* Move = Controller->GetPawn()->FindComponentByClass<UMRCharacterMovementComponent>())
	{
		Move->SetWantsToWalk(S.bWalk);
	}
	StepTime = 0.0;
	SampleStart = -1.0;
	MeasuredSpeed = 0.0;
	bRunning = true;
}

void UMRMoveTest::Tick(float DeltaTime)
{
	APawn* Pawn = Controller.IsValid() ? Controller->GetPawn() : nullptr;
	if (!Pawn || !Steps.IsValidIndex(Index))
	{
		bRunning = false;
		return;
	}
	const FStep& S = Steps[Index];
	StepTime += DeltaTime;
	// a short settle after placing, then input
	if (StepTime > 0.3)
	{
		Pawn->AddMovementInput(S.Direction, 1.f);
	}
	// the speed over the step's second half, on the ground at full speed
	const double Half = 0.3 + (S.Seconds - 0.3) * 0.5;
	if (SampleStart < 0.0 && StepTime >= Half)
	{
		SampleStart = StepTime;
		SampleFrom = Pawn->GetActorLocation();
	}
	if (StepTime >= S.Seconds + 0.3)
	{
		if (SampleStart >= 0.0)
		{
			MeasuredSpeed = FVector::Dist2D(Pawn->GetActorLocation(), SampleFrom) / FMath::Max(1e-3, StepTime - SampleStart);
		}
		Finish();
	}
}

void UMRMoveTest::Finish()
{
	bRunning = false;
	const FStep& S = Steps[Index];
	FString Detail;
	const bool bPass = S.Check(Detail);
	Passed += bPass ? 1 : 0;
	UE_LOG(LogMeridian, Display, TEXT("MRMoveTest: %s  %s  (%s)"), bPass ? TEXT("PASS") : TEXT("FAIL"), *S.Label, *Detail);
	if (UMRCharacterMovementComponent* Move = Controller->GetPawn()->FindComponentByClass<UMRCharacterMovementComponent>())
	{
		Move->SetWantsToWalk(false);
	}
	FTimerHandle Timer;
	Controller->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this]() { Begin(Index + 1); }), 0.5f, false);
}

AActor* UMRMoveTest::Box(const FVector& Min, const FVector& Max)
{
	UWorld* World = Controller->GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>((Min + Max) * 0.5, FRotator::ZeroRotator, Params);
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Cube);
	C->SetWorldScale3D((Max - Min) / 100.0);  // the engine cube is 100 cm, centred
	C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Boxes.Add(A);
	return A;
}

void UMRMoveTest::ClearBoxes()
{
	for (const TWeakObjectPtr<AActor>& A : Boxes)
	{
		if (A.IsValid())
		{
			A->Destroy();
		}
	}
	Boxes.Reset();
}

void UMRMoveTest::Place(const FVector& FeetAt, const FVector& Facing)
{
	APawn* Pawn = Controller->GetPawn();
	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (Char && Char->GetCharacterMovement())
	{
		Char->GetCharacterMovement()->StopMovementImmediately();
	}
	Pawn->TeleportTo(FeetAt + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Facing.Rotation(), false, true);
	Controller->SetControlRotation(Facing.Rotation());
}

double UMRMoveTest::FeetZ() const
{
	const APawn* Pawn = Controller.IsValid() ? Controller->GetPawn() : nullptr;
	return Pawn ? Pawn->GetActorLocation().Z - Pawn->GetSimpleCollisionHalfHeight() : 0.0;
}
