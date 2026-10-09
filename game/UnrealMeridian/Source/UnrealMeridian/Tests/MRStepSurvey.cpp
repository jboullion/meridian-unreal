#include "Tests/MRStepSurvey.h"

#include "Character/MRCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/MRUnits.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMeridian.h"
#include "World/MRRoomMesh.h"
#include "World/MRRuntimeRoom.h"
#include "World/MRRuntimeRooms.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	constexpr int32 NumProbes = 32;
	constexpr double Fine = MRRoo::RooPerFine;
	// the original's limits (clientd3d move.c MAX_STEP_HEIGHT, game.c player.height), ROO units
	constexpr double StepRoo = 24.0 * Fine;
	constexpr double HeightRoo = 768.0;
	constexpr double SinkRoo[4] = {0.0, MRRoo::RooPerSquare / 5, 2 * MRRoo::RooPerSquare / 5, 3 * MRRoo::RooPerSquare / 5};
	constexpr double OpenSky = 1e6;
	// a crossing is tried when it climbs this much or passes under this little (cm)
	constexpr double MinClimbCm = 10.0;
	constexpr double LowHeadCm = 200.0;
	// how far past the wall the probe's centre must get (cm), and how long it may take (s)
	constexpr double PastCm = 15.0;
	constexpr double TimeoutS = 1.5;
	// rooms built at runtime: far from the zones, each under its own zone id
	const FVector RuntimeOrigin(-600000.0, -600000.0, 0.0);
	constexpr int32 RuntimeRidBase = 990000;

	FString RoomsDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT(".."),
			TEXT("ReferenceServers"), TEXT("Server-104"), TEXT("resource"), TEXT("rooms")));
	}

	double FloorRoo(const FMRRooSector& S, double X, double Y)
	{
		return S.bSlopedFloor && S.FloorSlope.IsValid() ? S.FloorSlope.HeightAt(X, Y) : S.FloorHeight * Fine;
	}

	double CeilRoo(const FMRRooSector& S, double X, double Y)
	{
		return S.bSlopedCeiling && S.CeilingSlope.IsValid() ? S.CeilingSlope.HeightAt(X, Y) : S.CeilingHeight * Fine;
	}
}

// ------------------------------------------------------------------------------------- probe

AMRStepProbe::AMRStepProbe(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCapsuleComponent()->InitCapsuleSize(UMRCharacterMovementComponent::CapsuleRadiusCm, UMRCharacterMovementComponent::CapsuleHeightCm / 2.f);
	// probes walk through each other (and the player and monsters), not through the world
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->bRunPhysicsWithNoController = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	SetActorEnableCollision(true);
}

// ------------------------------------------------------------------------------------- survey

bool UMRStepSurvey::IsRequested()
{
	FString Value;
	return FParse::Param(FCommandLine::Get(), TEXT("MRStepSurvey")) || FParse::Value(FCommandLine::Get(), TEXT("MRStepSurvey="), Value);
}

TStatId UMRStepSurvey::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRStepSurvey, STATGROUP_Tickables);
}

void UMRStepSurvey::Start(APlayerController* InController)
{
	Controller = InController;
	UWorld* World = InController->GetWorld();
	const UMRZoneSubsystem* Zones = World->GetSubsystem<UMRZoneSubsystem>();

	FString Which;
	FParse::Value(FCommandLine::Get(), TEXT("MRStepSurvey="), Which, false);  // (a list: not cut at its commas)
	TArray<FString> All;
	IFileManager::Get().FindFiles(All, *RoomsDir(), TEXT("*.roo"));
	All.Sort();
	if (Which.IsEmpty() || Which == TEXT("built"))
	{
		for (const FString& F : All)
		{
			if (Zones && Zones->RidForRoom(F))
			{
				RoomFiles.Add(F);
			}
		}
	}
	else if (Which == TEXT("all"))
	{
		RoomFiles = All;
	}
	else
	{
		Which.ParseIntoArray(RoomFiles, TEXT(","));
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 i = 0; i < NumProbes; ++i)
	{
		const FVector Park = RuntimeOrigin + FVector(-20000.0 - i * 200.0, 0.0, 100000.0);
		AMRStepProbe* Probe = World->SpawnActor<AMRStepProbe>(AMRStepProbe::StaticClass(), FTransform(Park), Params);
		Probe->GetCharacterMovement()->DisableMovement();
		Probes.Add(Probe);
	}
	Runs.SetNum(NumProbes);
	Csv.Add(TEXT("room,wall,from,to,x_m,y_m,climb_cm,head_cm,depth_from,depth_to,sloped,no_lower_texture,result,rose_cm,past_cm,blocker"));
	UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: starting, %d rooms, %d probes (capsule %.0f x %.0f cm, step %.1f cm)"), RoomFiles.Num(), NumProbes,
		UMRCharacterMovementComponent::CapsuleRadiusCm, UMRCharacterMovementComponent::OriginalHeightCm, UMRCharacterMovementComponent::OriginalStepCm);
	Started = FPlatformTime::Seconds();
	ReadyAt = Started + 10.0;  // the zones stream in (real seconds: -benchmark runs game time faster)
	bRunning = true;
}

bool UMRStepSurvey::BeginRoom()
{
	UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	UMRZoneSubsystem* Zones = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	while (World && Zones && ++RoomIndex < RoomFiles.Num())
	{
		RoomFile = RoomFiles[RoomIndex];
		TArray<uint8> Bytes;
		FMRRooFile Room;
		FString Error;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(RoomsDir(), RoomFile)) || !Room.Load(Bytes, Error))
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRStepSurvey: %s: couldn't read it (%s)"), *RoomFile, *Error);
			continue;
		}
		const int32 BuiltRid = Zones->RidForRoom(RoomFile);
		const FMRZoneInfo* Zone = BuiltRid ? Zones->FindZone(BuiltRid) : nullptr;
		bRuntime = Zone == nullptr;
		FVector Origin = Zone ? Zone->Origin : RuntimeOrigin;
		if (bRuntime)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AMRRuntimeRoom* Actor = World->SpawnActor<AMRRuntimeRoom>(AMRRuntimeRoom::StaticClass(), FTransform(Origin), Params);
			Actor->SetRoomFile(RoomFile);
			Actor->Build(Room, {}, {}, nullptr);
			RuntimeRoom = Actor;
			RuntimeRid = RuntimeRidBase + RoomIndex;
			FMRZoneInfo Info;
			Info.Rid = RuntimeRid;
			Info.GeometryRid = RuntimeRid;
			Info.Name = RoomFile;
			Info.KodClass = TEXT("Survey");
			Info.Origin = Origin;
			Zones->AddRuntimeZone(Info, UMRRuntimeRooms::ToDepthAreas(Actor->GetDepthAreas()));
			Zones->SetRuntimeStepWalls(RuntimeRid, UMRRuntimeRooms::ToStepWalls(Actor->GetStepWalls()));
		}
		Crossings.Reset();
		Next = 0;
		RoomTried = RoomCrossed = RoomSkipped = RoomOverCap = 0;
		ListCrossings(Room, Origin);
		if (Crossings.Num() == 0)
		{
			UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: %s: nothing to try"), *RoomFile);
			EndRoom();
			continue;
		}
		FHitResult Floor;
		const FVector At = Crossings[0].Start;
		if (!bRuntime && !World->LineTraceSingleByChannel(Floor, At + FVector(0.0, 0.0, 50.0), At - FVector(0.0, 0.0, 50.0), ECC_WorldStatic))
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRStepSurvey: %s: no floor at its first crossing; is the zone loaded?"), *RoomFile);
		}
		return true;
	}
	return false;
}

void UMRStepSurvey::ListCrossings(const FMRRooFile& Room, const FVector& Origin)
{
	struct FLeaf
	{
		const TArray<FVector2f>* Points;
		int32 Sector;
		FBox2D Box;
	};
	TArray<FLeaf> Leaves;
	for (const FMRRooNode& N : Room.Nodes)
	{
		if (N.Type == FMRRooNode::Leaf && N.Sector && N.Points.Num() >= 3)
		{
			FBox2D Box(ForceInit);
			for (const FVector2f& P : N.Points)
			{
				Box += FVector2D(P);
			}
			Leaves.Add({&N.Points, N.Sector, Box.ExpandBy(1.0)});
		}
	}
	auto SectorAt = [&Leaves](const FVector2D& P)
	{
		for (const FLeaf& L : Leaves)
		{
			if (!L.Box.IsInside(P))
			{
				continue;
			}
			const TArray<FVector2f>& Pts = *L.Points;
			int32 Sign = 0;
			bool bIn = true;
			for (int32 i = 0; i < Pts.Num() && bIn; ++i)
			{
				const double C = FVector2D::CrossProduct(FVector2D(Pts[(i + 1) % Pts.Num()] - Pts[i]), P - FVector2D(Pts[i]));
				const int32 S = C > 1e-6 ? 1 : (C < -1e-6 ? -1 : 0);
				bIn = S == 0 || Sign == 0 || S == Sign;
				Sign = Sign == 0 ? S : Sign;
			}
			if (bIn)
			{
				return L.Sector;
			}
		}
		return 0;
	};
	auto ToWorld = [&Origin](const FVector2D& P, double Z)
	{
		return Origin + FVector(P.X, P.Y, Z) * MRUnits::CmPerRoo;
	};

	const double Radius = UMRCharacterMovementComponent::CapsuleRadiusCm;
	TSet<FString> Seen;
	for (int32 Wi = 0; Wi < Room.Walls.Num(); ++Wi)
	{
		const FMRRooWall& W = Room.Walls[Wi];
		if (!W.PosSector || !W.NegSector || W.PosSector == W.NegSector)
		{
			continue;
		}
		const FVector2D V0(W.X0, W.Y0), V1(W.X1, W.Y1);
		const double LenRoo = FVector2D::Distance(V0, V1);
		const double LenCm = LenRoo * MRUnits::CmPerRoo;
		const FString Key = FString::Printf(TEXT("%.0f,%.0f,%.0f,%.0f,%d,%d"), W.X0, W.Y0, W.X1, W.Y1, W.PosSector, W.NegSector);
		if (LenCm < 80.0 || Seen.Contains(Key))
		{
			continue;  // too short for the capsule to cross the middle of
		}
		Seen.Add(Key);
		const FVector2D Normal = FVector2D(-(V1.Y - V0.Y), V1.X - V0.X) / LenRoo;
		TArray<double> Ts;
		if (LenCm >= 400.0)
		{
			Ts = {0.2, 0.5, 0.8};
		}
		else
		{
			Ts = {0.5};
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 A = Side == 0 ? W.PosSector : W.NegSector;
			const int32 B = Side == 0 ? W.NegSector : W.PosSector;
			const FMRRooSidedef* Sd = Room.Sidedef(Side == 0 ? W.PosSidedef : W.NegSidedef);
			const FMRRooSector* SA = Room.Sector(A);
			const FMRRooSector* SB = Room.Sector(B);
			if (!Sd || !SA || !SB || !(Sd->Flags & MRRoo::WF_PASSABLE))
			{
				continue;
			}
			const int32 DA = SA->Depth(), DB = SB->Depth();
			// the original measures the step at the wall's first end (clientd3d bspload.c SetWallHeights: z1, z2)
			const double Z1 = FMath::Max(FloorRoo(*SA, V0.X, V0.Y), FloorRoo(*SB, V0.X, V0.Y));
			const double Z2 = FMath::Min(CeilRoo(*SA, V0.X, V0.Y), CeilRoo(*SB, V0.X, V0.Y));
			for (const double T : Ts)
			{
				const FVector2D P = V0 + (V1 - V0) * T;
				FVector2D N = Normal;
				const double Probe = 16.0;
				if (SectorAt(P + N * Probe) != B || SectorAt(P - N * Probe) != A)
				{
					N = -N;
					if (SectorAt(P + N * Probe) != B || SectorAt(P - N * Probe) != A)
					{
						continue;  // the wall isn't between them here (a corner, another wall in the way)
					}
				}
				const double Z = FloorRoo(*SA, P.X, P.Y) - SinkRoo[DA];   // where the player stands (GetFloorBase)
				if (CeilRoo(*SA, P.X, P.Y) - Z < HeightRoo)
				{
					continue;  // no one stands here: the server keeps you out (a closed door or lift; open sky or not)
				}
				const double FloorB = FloorRoo(*SB, P.X, P.Y) - SinkRoo[DB];
				const double CeilB = CeilRoo(*SB, P.X, P.Y);
				// the original client (move.c IntersectNode) and the server (roofile.c BSPCanMoveInRoomTreeInternal)
				const bool bStep = !Sd->BelowTexture || Z1 - SinkRoo[DB] - Z <= StepRoo;
				const bool bHead = !Sd->AboveTexture || Z2 - Z >= HeightRoo;
				const bool bServer = CeilB - FloorB >= HeightRoo && (!Sd->AboveTexture || CeilB - Z >= HeightRoo);
				if (!bStep || !bHead || !bServer)
				{
					continue;
				}
				// our collision: the upper wall (unless both are open sky) and the far ceiling (unless sky)
				const bool bBothSky = !SA->CeilingTexture && !SB->CeilingTexture;
				const double Upper = bBothSky ? OpenSky : FMath::Min(CeilRoo(*SA, P.X, P.Y), CeilB);
				const double Ceiling = FMath::Min(Upper, SB->CeilingTexture ? CeilB : OpenSky);
				FCrossing C;
				C.ClimbCm = (FloorB - Z) * MRUnits::CmPerRoo;
				C.HeadCm = Ceiling >= OpenSky ? OpenSky : (Ceiling - FMath::Max(Z, FloorB)) * MRUnits::CmPerRoo;
				if (C.ClimbCm < MinClimbCm && C.HeadCm >= LowHeadCm)
				{
					continue;
				}
				if (C.ClimbCm > UMRCharacterMovementComponent::StepCapCm())
				{
					++RoomOverCap;
					continue;  // higher than we let the original's rule climb (mr.Move.StepCapCm)
				}
				// start a little back from the wall, inside A
				bool bStart = false;
				for (const double BackCm : {Radius + 25.0, Radius + 12.0, Radius + 3.0})
				{
					const FVector2D S = P - N * (BackCm / MRUnits::CmPerRoo);
					if (SectorAt(S) == A)
					{
						C.Start = ToWorld(S, FloorRoo(*SA, S.X, S.Y) - SinkRoo[DA]);
						bStart = true;
						break;
					}
				}
				if (!bStart)
				{
					++RoomSkipped;
					continue;
				}
				C.Wall = Wi;
				C.From = A;
				C.To = B;
				C.AtRoo = P;
				C.Dir = N;
				C.WallPoint = FVector2D(ToWorld(P, 0.0));
				C.DepthFrom = DA;
				C.DepthTo = DB;
				C.bSloped = SA->bSlopedFloor || SB->bSlopedFloor;
				C.bNoLowerTexture = !Sd->BelowTexture;
				Crossings.Add(C);
			}
		}
	}
}

void UMRStepSurvey::EndRoom()
{
	if (RuntimeRoom.IsValid())
	{
		RuntimeRoom->Destroy();
	}
	RuntimeRoom.Reset();
	if (RuntimeRid)
	{
		if (UMRZoneSubsystem* Zones = Controller.IsValid() ? Controller->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr)
		{
			Zones->RemoveRuntimeZone(RuntimeRid);
		}
		RuntimeRid = 0;
	}
	if (RoomTried > 0 || RoomSkipped > 0)
	{
		UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: %s%s: %d/%d crossed%s"), *RoomFile, bRuntime ? TEXT("") : TEXT(" (built)"), RoomCrossed, RoomTried,
			RoomSkipped ? *FString::Printf(TEXT(", %d with no room to start"), RoomSkipped) : TEXT(""));
	}
	Tried += RoomTried;
	Crossed += RoomCrossed;
	Skipped += RoomSkipped;
	OverCap += RoomOverCap;
	Crossings.Reset();
}

void UMRStepSurvey::Tick(float DeltaTime)
{
	if (!Controller.IsValid())
	{
		bRunning = false;
		return;
	}
	if (FPlatformTime::Seconds() < ReadyAt)
	{
		return;
	}
	if (RoomIndex < 0 || (Crossings.Num() == 0 && RoomIndex < RoomFiles.Num()))
	{
		if (!BeginRoom())
		{
			Finish();
			return;
		}
	}
	UWorld* World = Controller->GetWorld();
	const float Half = UMRCharacterMovementComponent::CapsuleHeightCm / 2.f;
	bool bBusy = false;
	for (int32 i = 0; i < Probes.Num(); ++i)
	{
		AMRStepProbe* Probe = Probes[i].Get();
		if (!Probe)
		{
			continue;
		}
		FRun& R = Runs[i];
		if (R.Crossing >= 0)
		{
			bBusy = true;
			const FCrossing& C = Crossings[R.Crossing];
			R.Time += DeltaTime;
			Probe->AddMovementInput(FVector(C.Dir, 0.0), 1.f);
			const double Past = FVector2D::DotProduct(FVector2D(Probe->GetActorLocation()) - C.WallPoint, C.Dir);
			// across, and standing on the far floor (not falling through it)
			const double Feet = Probe->GetActorLocation().Z - Half;
			const bool bStanding = !Probe->GetCharacterMovement()->IsFalling() && Feet > C.Start.Z + FMath::Min(C.ClimbCm, 0.0) - 20.0;
			// or up the step, though a thin sector's next wall keeps its centre from getting past (a stair)
			const bool bUp = C.ClimbCm >= MinClimbCm && Feet >= C.Start.Z + C.ClimbCm - 10.0
				&& Past >= -UMRCharacterMovementComponent::CapsuleRadiusCm;
			if (bStanding && (Past >= PastCm || bUp))
			{
				Judge(i, false);
			}
			else if (R.Time > TimeoutS)
			{
				Judge(i, true);
			}
			continue;
		}
		// the next crossing with room to stand at its start
		while (Next < Crossings.Num())
		{
			const FCrossing& C = Crossings[Next];
			const FVector At = C.Start + FVector(0.0, 0.0, Half + 2.0);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MRStepSurvey), false, Probe);
			FCollisionResponseParams Response;
			Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
			TArray<FOverlapResult> Overlaps;
			World->OverlapMultiByChannel(Overlaps, At, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(UMRCharacterMovementComponent::CapsuleRadiusCm - 1.f, Half - 1.f), Query, Response);
			const FOverlapResult* Blocking = Overlaps.FindByPredicate([](const FOverlapResult& O) { return O.bBlockingHit; });
			if (Blocking)
			{
				// what's there: a prop (worth a look: it may stand on a step), or the room's own walls
				++RoomSkipped;
				const AActor* Who = Blocking->GetActor();
				Csv.Add(FString::Printf(TEXT("%s,%d,%d,%d,%.2f,%.2f,%.0f,%.0f,%d,%d,%d,%d,no_room,0,0,\"%s\""), *RoomFile, C.Wall, C.From, C.To,
					C.AtRoo.X * MRUnits::CmPerRoo / 100.0, C.AtRoo.Y * MRUnits::CmPerRoo / 100.0, C.ClimbCm, FMath::Min(C.HeadCm, 9999.0),
					C.DepthFrom, C.DepthTo, C.bSloped ? 1 : 0, C.bNoLowerTexture ? 1 : 0, Who ? *Who->GetName() : TEXT("?")));
				++Next;
				continue;
			}
			Probe->SetActorLocationAndRotation(At, FVector(C.Dir, 0.0).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			UCharacterMovementComponent* Move = Probe->GetCharacterMovement();
			Move->StopMovementImmediately();
			Move->SetMovementMode(MOVE_Falling);
			Move->SetMovementMode(MOVE_Walking);
			R.Crossing = Next++;
			R.Time = 0.0;
			bBusy = true;
			break;
		}
	}
	if (!bBusy && Next >= Crossings.Num())
	{
		EndRoom();
	}
}

void UMRStepSurvey::Judge(int32 ProbeIndex, bool bTimedOut)
{
	AMRStepProbe* Probe = Probes[ProbeIndex].Get();
	FRun& R = Runs[ProbeIndex];
	const FCrossing& C = Crossings[R.Crossing];
	const float Half = UMRCharacterMovementComponent::CapsuleHeightCm / 2.f;
	const FVector At = Probe->GetActorLocation();
	const double Rose = At.Z - Half - C.Start.Z;
	const double Past = FVector2D::DotProduct(FVector2D(At) - C.WallPoint, C.Dir);
	++RoomTried;
	FString Blocker;
	if (bTimedOut)
	{
		// what's in the way: a short sweep on from where it stopped
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MRStepSurvey), false, Probe);
		FCollisionResponseParams Response;
		Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		if (Probe->GetWorld()->SweepSingleByChannel(Hit, At, At + FVector(C.Dir, 0.0) * 30.0, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(UMRCharacterMovementComponent::CapsuleRadiusCm, Half), Query, Response))
		{
			Blocker = Hit.GetActor() ? Hit.GetActor()->GetName() : TEXT("?");
			Blocker += FString::Printf(TEXT(" (hit %.0f cm above the feet, normal z %.2f)"), Hit.ImpactPoint.Z - (At.Z - Half), Hit.ImpactNormal.Z);
		}
		else
		{
			Blocker = Probe->GetCharacterMovement()->IsFalling() ? TEXT("falling") : TEXT("nothing in the way");
		}
	}
	else
	{
		++RoomCrossed;
	}
	const FString Line = FString::Printf(TEXT("%s,%d,%d,%d,%.2f,%.2f,%.0f,%.0f,%d,%d,%d,%d,%s,%.0f,%.0f,\"%s\""), *RoomFile, C.Wall, C.From, C.To,
		C.AtRoo.X * MRUnits::CmPerRoo / 100.0, C.AtRoo.Y * MRUnits::CmPerRoo / 100.0, C.ClimbCm, FMath::Min(C.HeadCm, 9999.0), C.DepthFrom, C.DepthTo,
		C.bSloped ? 1 : 0, C.bNoLowerTexture ? 1 : 0, bTimedOut ? TEXT("stuck") : TEXT("crossed"), Rose, Past, *Blocker);
	Csv.Add(Line);
	if (bTimedOut)
	{
		UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: FAIL %s wall %d, sector %d -> %d at (%.1f, %.1f) m: climb %.0f cm, room %.0f cm%s%s%s; rose %.0f, %.0f cm past; %s"),
			*RoomFile, C.Wall, C.From, C.To, C.AtRoo.X * MRUnits::CmPerRoo / 100.0, C.AtRoo.Y * MRUnits::CmPerRoo / 100.0, C.ClimbCm,
			FMath::Min(C.HeadCm, 9999.0), C.DepthFrom || C.DepthTo ? *FString::Printf(TEXT(", depth %d -> %d"), C.DepthFrom, C.DepthTo) : TEXT(""),
			C.bSloped ? TEXT(", sloped") : TEXT(""), C.bNoLowerTexture ? TEXT(", no lower texture") : TEXT(""), Rose, Past, *Blocker);
	}
	// park it until the next crossing
	Probe->GetCharacterMovement()->DisableMovement();
	Probe->SetActorLocation(RuntimeOrigin + FVector(-20000.0 - ProbeIndex * 200.0, 0.0, 100000.0), false, nullptr, ETeleportType::TeleportPhysics);
	R.Crossing = -1;
}

void UMRStepSurvey::Finish()
{
	bRunning = false;
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MRStepSurvey"), TEXT("results.csv"));
	FFileHelper::SaveStringArrayToFile(Csv, *Path);
	UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: %d with no room to start (a prop, or the room too tight for the capsule)"), Skipped);
	UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: %d higher than mr.Move.StepCapCm (%.0f cm), not tried"), OverCap, UMRCharacterMovementComponent::StepCapCm());
	UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: wrote %s"), *FPaths::ConvertRelativePathToFull(Path));
	UE_LOG(LogMeridian, Display, TEXT("MRStepSurvey: DONE %d/%d (%.0f s)"), Crossed, Tried, FPlatformTime::Seconds() - Started);
	for (const TWeakObjectPtr<AMRStepProbe>& P : Probes)
	{
		if (P.IsValid())
		{
			P->Destroy();
		}
	}
	if (Controller.IsValid())
	{
		Controller->ConsoleCommand(TEXT("quit"));
	}
}
