#include "Tests/MRZoneSmokeTest.h"

#include "Character/MRCharacterMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"
#include "Zones/MRZoneSubsystem.h"

bool UMRZoneSmokeTest::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRZoneTest"));
}

void UMRZoneSmokeTest::Start(APawn* InPawn)
{
	Pawn = InPawn;
	// Positions are original (row, col) squares from data/zones.json.
	Steps = {
		{TEXT("spawned in the Raza Inn"),                 0,   0,  0, 301},
		{TEXT("Inn door (9,6) -> Raza town"),             301, 9,  6, 300},
		{TEXT("Mausoleum door (27,24) -> Mausoleum"),     300, 27, 24, 306},
		{TEXT("Mausoleum exit (11,17) -> Raza town"),     306, 11, 17, 300},
		{TEXT("Hall door (6,10) -> Adventurer's Hall"),   300, 6,  10, 302},
		{TEXT("Hall exit (5,9) -> Raza town"),            302, 5,  9,  300},
		{TEXT("north of the town wall -> Outskirts (no teleport)"), 300, 0, 38, 330, false},
		{TEXT("back inside the wall -> Raza town (no teleport)"),   330, 46, 40, 300},
		// Raza's east wheat field (depth 2): a block 0.86 m above the grass that you wade through
		{TEXT("into the wheat field (11,62): wading, slowed"),      300, 11, 62, 300, true, false, 0.f, 2},
		// the forest's north passage crosses the grid edge at columns 33-36
		{TEXT("north passage off the Outskirts' edge -> Western Farol"), 330, 0, 34, 331},
		{TEXT("Western Farol south passage -> Outskirts"),                331, 51, 24, 330},
		{TEXT("into Western Farol again"),                                 330, 0, 34, 331},
		// After 35 s in Farol the client has dropped the town's buildings (30 s retention), so
		// this exercises the server asking the client to stream a far zone and waiting for it.
		{TEXT("server teleport from Farol to the Bank (not a neighbour)"), 333, 5, 2, 333, true, true, 35.f},
	};
	Index = 0;
	Passed = 0;
	UE_LOG(LogMeridian, Display, TEXT("MRZoneTest: starting, %d steps"), Steps.Num());
	InPawn->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRZoneSmokeTest::RunStep), 2.0f, false);
}

void UMRZoneSmokeTest::RunStep()
{
	APawn* P = Pawn.Get();
	UMRZoneSubsystem* Zones = P ? P->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!P || !Zones)
	{
		UE_LOG(LogMeridian, Error, TEXT("MRZoneTest: pawn or zone subsystem gone, aborting"));
		return;
	}
	const FStep& S = Steps[Index];
	if (S.bServerTeleport)
	{
		if (TeleportStart == 0.0)
		{
			TeleportStart = FPlatformTime::Seconds();
		}
		if (!Zones->TeleportPawn(P, S.PlaceZone, S.Row, S.Col))
		{
			// the server is waiting for the client to stream the destination; try again shortly
			P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRZoneSmokeTest::RunStep), 0.05f, false);
			return;
		}
		UE_LOG(LogMeridian, Display, TEXT("MRZoneTest:   server teleport completed after %.0f ms"),
			(FPlatformTime::Seconds() - TeleportStart) * 1000.0);
		TeleportStart = 0.0;
	}
	else if (S.PlaceZone != 0)
	{
		FVector Dest = Zones->GridToWorld(S.PlaceZone, S.Row, S.Col, S.bNeedsFloor);
		if (!S.bNeedsFloor)
		{
			// edge crossings only care about XY; keep the pawn's current height band
			Dest.Z = Zones->GridToWorld(S.PlaceZone, S.Row + (S.Row <= 0 ? 2 : 0), S.Col, true).Z;
		}
		Dest.Z += P->GetSimpleCollisionHalfHeight() + 5.0;
		const bool bMoved = P->TeleportTo(Dest, P->GetActorRotation(), false, true);
		UE_LOG(LogMeridian, Display, TEXT("MRZoneTest:   placed at zone %d (%d,%d) -> %s  ok=%d  now %s"),
			S.PlaceZone, S.Row, S.Col, *Dest.ToCompactString(), bMoved, *P->GetActorLocation().ToCompactString());
	}
	P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRZoneSmokeTest::CheckStep), 1.0f, false);
}

void UMRZoneSmokeTest::CheckStep()
{
	APawn* P = Pawn.Get();
	if (!P)
	{
		return;
	}
	const FStep& S = Steps[Index];
	const AMRPlayerState* PS = P->GetPlayerState<AMRPlayerState>();
	const int32 Zone = PS ? PS->GetZoneId() : -1;
	UMRZoneSubsystem* Zones = P->GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const FIntPoint Grid = Zones ? Zones->WorldToGrid(Zone, P->GetActorLocation()) : FIntPoint::ZeroValue;

	// a pawn that fell through the world would be far below its zone
	const FMRZoneInfo* Info = Zones ? Zones->FindZone(Zone) : nullptr;
	const bool bGrounded = Info && P->GetActorLocation().Z > Info->Origin.Z - 2000.0;
	// the player's client must have streamed the zone's geometry (reported to the server)
	const bool bClientHasZone = Zones && Zones->IsZoneReadyFor(P->GetController(), Zone);
	bool bPass = Zone == S.ExpectZone && bGrounded && bClientHasZone;
	if (S.ExpectDepth && Zones && Info)
	{
		// feet at the wading floor, about the grass's height, not on the wheat 1.2 m up
		const double FeetM = (P->GetActorLocation().Z - P->GetSimpleCollisionHalfHeight() - Info->Origin.Z) / 100.0;
		const int32 Depth = Zones->DepthAt(P->GetActorLocation());
		const UMRCharacterMovementComponent* Move = P->FindComponentByClass<UMRCharacterMovementComponent>();
		const float Factor = Move ? Move->GetMaxSpeed() / Move->RunSpeed : -1.f;
		const bool bWading = Depth == S.ExpectDepth && FeetM < 0.7 && FMath::IsNearlyEqual(Factor, UMRZoneSubsystem::DepthSpeedFactor(Depth), 0.01f);
		UE_LOG(LogMeridian, Display, TEXT("MRZoneTest:   wading: depth %d (expected %d), feet at %.2f m, speed x%.2f"),
			Depth, S.ExpectDepth, FeetM, Factor);
		bPass = bPass && bWading;
	}
	Passed += bPass ? 1 : 0;
	UE_LOG(LogMeridian, Display, TEXT("MRZoneTest: %s  %s  (zone %d, expected %d, square %d,%d, z=%.0f, client has level=%d)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *S.Label, Zone, S.ExpectZone, Grid.X, Grid.Y, P->GetActorLocation().Z,
		bClientHasZone ? 1 : 0);

	if (++Index < Steps.Num())
	{
		const float Delay = 0.5f + Steps[Index].PreDelay;
		if (Steps[Index].PreDelay > 0.f)
		{
			UE_LOG(LogMeridian, Display, TEXT("MRZoneTest:   waiting %.0f s"), Steps[Index].PreDelay);
		}
		P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRZoneSmokeTest::RunStep), Delay, false);
	}
	else
	{
		UE_LOG(LogMeridian, Display, TEXT("MRZoneTest: DONE %d/%d passed"), Passed, Steps.Num());
	}
}
