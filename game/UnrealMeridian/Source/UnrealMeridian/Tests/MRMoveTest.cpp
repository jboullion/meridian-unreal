#include "Tests/MRMoveTest.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Character/MRCharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"
#include "Core/MRUnits.h"
#include "World/MRRuntimeRoom.h"
#include "World/MRRuntimeRooms.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	// the test rig: empty space well away from the zones' 2 km grid
	const FVector Rig(-300000.0, -300000.0, 0.0);
	// the test room's zone id (UMRZoneSubsystem::AddRuntimeZone)
	constexpr int32 TestRoomRid = 999999;
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
	const double OriginalStepCm = UMRCharacterMovementComponent::OriginalStepCm;
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
	// a wading pool of the given depth over the rig's west half, its collision floor sunk as the zones'
	// are (roo2gltf), and a bank to the east BankCm above the pool's own floor (0: level with it)
	auto Pool = [this, SetZone](int32 Depth, double BankCm)
	{
		return [this, SetZone, Depth, BankCm]()
		{
			SetZone(0);
			ClearBoxes();
			const double Sink = UMRZoneSubsystem::DepthSinkCm(Depth);
			Box(Rig + FVector(-3000.0, -1000.0, -Sink - 100.0), Rig + FVector(3000.0, 1000.0, -Sink));
			Box(Rig + FVector(0.0, -1000.0, -Sink), Rig + FVector(3000.0, 1000.0, BankCm));
			SetTestPool(FBox2D(FVector2D(Rig) + FVector2D(-3000.0, -1000.0), FVector2D(Rig) + FVector2D(0.0, 1000.0)), Depth);
			Place(Rig + FVector(-400.0, 0.0, -Sink), FVector::ForwardVector);
		};
	};
	// a step StepCm high across the rig, 3 m ahead of the pawn; Beam: a beam that high instead, to walk under
	auto Step = [this, SetZone](double StepCm)
	{
		return [this, SetZone, StepCm]()
		{
			SetZone(0);
			ClearBoxes();
			Box(Rig + FVector(-3000.0, -1000.0, -100.0), Rig + FVector(3000.0, 1000.0, 0.0));
			Box(Rig + FVector(0.0, -1000.0, 0.0), Rig + FVector(3000.0, 1000.0, StepCm));
			Place(Rig + FVector(-300.0, 0.0, 0.0), FVector::ForwardVector);
		};
	};
	auto Beam = [this, SetZone](double UnderCm)
	{
		return [this, SetZone, UnderCm]()
		{
			SetZone(0);
			ClearBoxes();
			Box(Rig + FVector(-3000.0, -1000.0, -100.0), Rig + FVector(3000.0, 1000.0, 0.0));
			Box(Rig + FVector(0.0, -1000.0, UnderCm), Rig + FVector(100.0, 1000.0, UnderCm + 100.0));
			Place(Rig + FVector(-300.0, 0.0, 0.0), FVector::ForwardVector);
		};
	};
	// the same step between two sectors of a room built at runtime (AMRRuntimeRoom, as online), Kod units
	auto RoomStep = [this, SetZone](int16 StepKod, bool bLowerTexture = true)
	{
		return [this, SetZone, StepKod, bLowerTexture]()
		{
			SetZone(0);
			ClearBoxes();
			RuntimeStep(StepKod, bLowerTexture, false);
			Place(Rig + FVector(-300.0, 0.0, 0.0), FVector::ForwardVector);
		};
	};
	// a level room with a one-way wall at the rig's x, the pawn 3 m west (bFromWest) or east of it
	auto OneWay = [this, SetZone](bool bFromWest)
	{
		return [this, SetZone, bFromWest]()
		{
			SetZone(0);
			ClearBoxes();
			RuntimeStep(0, true, true);
			Place(Rig + FVector(bFromWest ? -300.0 : 300.0, 0.0, 0.0), bFromWest ? FVector::ForwardVector : -FVector::ForwardVector);
		};
	};
	auto Climbed = [this](double StepCm)
	{
		return [this, StepCm](FString& Out)
		{
			const FVector P = Controller->GetPawn()->GetActorLocation();
			Out = FString::Printf(TEXT("feet at %.0f cm, %.0f cm past the step's edge (its top: %.1f)"), FeetZ(), P.X - Rig.X, StepCm);
			return FMath::Abs(FeetZ() - StepCm) < 10.0 && P.X - Rig.X > 100.0;
		};
	};
	auto Stopped = [this](double StepCm)
	{
		return [this, StepCm](FString& Out)
		{
			const FVector P = Controller->GetPawn()->GetActorLocation();
			Out = FString::Printf(TEXT("feet at %.0f cm, %.0f cm from the step's edge (its top: %.1f)"), FeetZ(), P.X - Rig.X, StepCm);
			return FeetZ() < 10.0 && P.X - Rig.X < 0.0;
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
		{TEXT("wading at depth 2: half speed"), Pool(2, 0.0), -FVector::ForwardVector, false, 2.f, SpeedCheck(Run * 0.5f)},
		// the original lets you walk out of a pool onto a bank level with its floor (a wall without a
		// lower texture never blocks a step), though the pool sinks you 0.88 or 1.32 m below it
		{TEXT("out of a depth 3 pool onto a level bank (1.32 m up)"), Pool(3, 0.0), FVector::ForwardVector, false, 3.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("feet at %.0f cm, %.0f cm onto the bank (bank: 0)"), FeetZ(), P.X - Rig.X);
				return FeetZ() > -10.0 && P.X - Rig.X > 100.0;
			}},
		{TEXT("out of a depth 2 pool onto a level bank (0.88 m up)"), Pool(2, 0.0), FVector::ForwardVector, false, 2.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("feet at %.0f cm, %.0f cm onto the bank (bank: 0)"), FeetZ(), P.X - Rig.X);
				return FeetZ() > -10.0 && P.X - Rig.X > 100.0;
			}},
		// a bank higher than the pool's floor is a step from where the pool sinks you: 0.55 + 0.44 m, too high
		{TEXT("depth 1 pool, bank 0.55 m above its floor (0.99 m up): stays in"), Pool(1, 55.0), FVector::ForwardVector, false, 2.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("feet at %.0f cm, %.0f cm from the bank (pool floor: -44)"), FeetZ(), P.X - Rig.X);
				return FeetZ() < -30.0 && P.X - Rig.X < 0.0;
			}},
		// terrain: the original climbs a step up to 24 Kod units (0.825 m) from where you stand, at any speed
		{TEXT("a 0.825 m step (the original's most): runs up it"), Step(OriginalStepCm), FVector::ForwardVector, false, 1.f, Climbed(OriginalStepCm)},
		{TEXT("a 0.825 m step: walks up it"), Step(OriginalStepCm), FVector::ForwardVector, true, 1.5f, Climbed(OriginalStepCm)},
		{TEXT("a 0.85 m step: stops at it"), Step(85.0), FVector::ForwardVector, false, 1.f, Stopped(85.0)},
		// the original's player is 1.65 m tall (game.c player.height): it passes under anything higher
		{TEXT("a beam 1.66 m up: walks under it"), Beam(166.0), FVector::ForwardVector, false, 1.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("%.0f cm past the beam's near side"), P.X - Rig.X);
				return P.X - Rig.X > 300.0;
			}},
		// rooms built at runtime: their collision once stopped every step (cooked double-sided, MRStepSurvey)
		{TEXT("room built at runtime: runs up a 24 Kod unit step between sectors"), RoomStep(24), FVector::ForwardVector, false, 1.f, Climbed(OriginalStepCm)},
		{TEXT("room built at runtime: walks up a 12 Kod unit step"), RoomStep(12), FVector::ForwardVector, true, 1.5f, Climbed(OriginalStepCm / 2.0)},
		{TEXT("room built at runtime: stops at a 25 Kod unit step"), RoomStep(25), FVector::ForwardVector, false, 1.f, Stopped(25.0 / 24.0 * OriginalStepCm)},
		// a wall without a lower texture never blocks a step in the original; we climb it up to mr.Move.StepCapCm (3 m)
		{TEXT("room built at runtime: climbs a 1.51 m step without a lower texture"), RoomStep(44, false), FVector::ForwardVector, false, 1.f,
			Climbed(44.0 / 24.0 * OriginalStepCm)},
		{TEXT("room built at runtime: stops at a 3.99 m step without a lower texture (over the cap)"), RoomStep(116, false), FVector::ForwardVector, false, 1.f,
			Stopped(116.0 / 24.0 * OriginalStepCm)},
		// the original checks the sidedef on the side you come from: a wall can be passable one way only
		{TEXT("room built at runtime: walks through a one-way wall from its passable side"), OneWay(true), FVector::ForwardVector, false, 1.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("%.0f cm past the wall"), P.X - Rig.X);
				return P.X - Rig.X > 200.0;
			}},
		{TEXT("room built at runtime: stopped by a one-way wall from the other side"), OneWay(false), -FVector::ForwardVector, false, 1.f,
			[this](FString& Out)
			{
				const FVector P = Controller->GetPawn()->GetActorLocation();
				Out = FString::Printf(TEXT("%.0f cm east of the wall"), P.X - Rig.X);
				return P.X - Rig.X > 20.0;
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
	Flight.Reset();
	bWasFalling = false;
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
	// judged once it's on the ground again (a slow frame used to catch a jump mid-air), within 2 s more
	const UCharacterMovementComponent* Move = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	const bool bInAir = Move && Move->IsFalling();
	if (StepTime > 0.3 && bInAir != bWasFalling && Flight.Len() < 200)
	{
		const FVector P = Pawn->GetActorLocation();
		if (bInAir)
		{
			TakeOffX = P.X - Rig.X;
		}
		else
		{
			Flight += FString::Printf(TEXT("%sfell from %.0f to %.0f cm past the ledge, down to feet %.0f"), Flight.IsEmpty() ? TEXT("") : TEXT("; "),
				TakeOffX, P.X - Rig.X, FeetZ());
		}
		bWasFalling = bInAir;
	}
	if (StepTime >= S.Seconds + 0.3 && (!bInAir || StepTime >= S.Seconds + 2.3))
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
	if (!Flight.IsEmpty())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRMoveTest:     %s"), *Flight);
	}
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
	SetTestPool(FBox2D(ForceInit), 0);
	if (UMRZoneSubsystem* Zones = Controller.IsValid() ? Controller->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr)
	{
		Zones->RemoveRuntimeZone(TestRoomRid);
	}
}

void UMRMoveTest::RuntimeStep(int16 StepKod, bool bLowerTexture, bool bOneWay)
{
	// two sectors side by side at the rig, the far one StepKod higher; the wall between them passable
	// with a lower texture (the original's step rule applies), open sky
	FMRRooFile Room;
	Room.Version = 15;
	const float Sq = MRRoo::RooPerSquare;
	const float Edge = 500.0f / MRUnits::CmPerRoo;   // the step: 5 m in from the room's west edge
	auto Rect = [](float X0, float Y0, float X1, float Y1, uint16 Sector)
	{
		FMRRooNode N;
		N.Type = FMRRooNode::Leaf;
		N.Sector = Sector;
		N.Points = {FVector2f(X0, Y0), FVector2f(X1, Y0), FVector2f(X1, Y1), FVector2f(X0, Y1)};
		for (const FVector2f& P : N.Points)
		{
			N.Bounds += P;
		}
		return N;
	};
	Room.Width = static_cast<int32>(8 * Sq);
	Room.Height = static_cast<int32>(4 * Sq);
	Room.Nodes.Add(Rect(0.f, 0.f, Edge, 4 * Sq, 1));
	Room.Nodes.Add(Rect(Edge, 0.f, 8 * Sq, 4 * Sq, 2));
	for (const int16 Floor : {int16(0), StepKod})
	{
		FMRRooSector& S = Room.Sectors.AddDefaulted_GetRef();
		S.FloorTexture = 1;
		S.FloorHeight = Floor;
		S.CeilingHeight = 1024;
		S.Light = 200;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		// one way: a textured wall between them that only the west side's sidedef lets you through
		FMRRooSidedef& Sd = Room.Sidedefs.AddDefaulted_GetRef();
		Sd.BelowTexture = bLowerTexture ? 1 : 0;
		Sd.NormalTexture = bOneWay ? 1 : 0;
		Sd.Flags = !bOneWay || i == 0 ? MRRoo::WF_PASSABLE : 0;
	}
	FMRRooWall& W = Room.Walls.AddDefaulted_GetRef();
	W.X0 = Edge;
	W.Y0 = 0.f;
	W.X1 = Edge;
	W.Y1 = 4 * Sq;
	W.Length = 4 * Sq;
	W.PosSidedef = 1;
	W.NegSidedef = 2;
	W.PosSector = 1;
	W.NegSector = 2;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// room-local x = Edge sits at the rig's x, room-local y = 2 squares on the rig's y
	const FVector Origin = Rig - FVector(Edge * MRUnits::CmPerRoo, 2 * Sq * MRUnits::CmPerRoo, 0.0);
	AMRRuntimeRoom* Actor = Controller->GetWorld()->SpawnActor<AMRRuntimeRoom>(AMRRuntimeRoom::StaticClass(), FTransform(Origin), Params);
	Actor->SetRoomFile(TEXT("movetest.roo"));
	Actor->Build(Room, {}, {}, nullptr);
	Boxes.Add(Actor);
	// a zone of its own, so the movement knows its walls (the original's step rule)
	if (UMRZoneSubsystem* Zones = Controller->GetWorld()->GetSubsystem<UMRZoneSubsystem>())
	{
		FMRZoneInfo Info;
		Info.Rid = TestRoomRid;
		Info.GeometryRid = TestRoomRid;
		Info.Name = TEXT("movetest.roo");
		Info.KodClass = TEXT("MoveTest");
		Info.Origin = Origin;
		Zones->AddRuntimeZone(Info, UMRRuntimeRooms::ToDepthAreas(Actor->GetDepthAreas()));
		Zones->SetRuntimeStepWalls(TestRoomRid, UMRRuntimeRooms::ToStepWalls(Actor->GetStepWalls()));
	}
}

void UMRMoveTest::SetTestPool(const FBox2D& Area, int32 Depth)
{
	if (UMRZoneSubsystem* Zones = Controller.IsValid() ? Controller->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr)
	{
		Zones->SetTestDepthArea(Area, Depth);
	}
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
