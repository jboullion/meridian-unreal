#include "Tests/MRZoneSmokeTest.h"

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
		// the forest's north passage crosses the grid edge at columns 33-36
		{TEXT("north passage off the Outskirts' edge -> Western Farol"), 330, 0, 34, 331},
		{TEXT("Western Farol south passage -> Outskirts"),                331, 51, 24, 330},
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
	if (S.PlaceZone != 0)
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
	const bool bPass = Zone == S.ExpectZone && bGrounded;
	Passed += bPass ? 1 : 0;
	UE_LOG(LogMeridian, Display, TEXT("MRZoneTest: %s  %s  (zone %d, expected %d, square %d,%d, z=%.0f)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *S.Label, Zone, S.ExpectZone, Grid.X, Grid.Y, P->GetActorLocation().Z);

	if (++Index < Steps.Num())
	{
		P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRZoneSmokeTest::RunStep), 0.5f, false);
	}
	else
	{
		UE_LOG(LogMeridian, Display, TEXT("MRZoneTest: DONE %d/%d passed"), Passed, Steps.Num());
	}
}
