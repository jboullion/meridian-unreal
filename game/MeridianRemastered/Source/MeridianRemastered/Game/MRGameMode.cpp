#include "Game/MRGameMode.h"

#include "Character/MRCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "MeridianRemastered.h"
#include "Player/MRPlayerController.h"
#include "Player/MRPlayerState.h"
#include "Tests/MRZoneSmokeTest.h"
#include "Zones/MRZoneSubsystem.h"

AMRGameMode::AMRGameMode()
{
	DefaultPawnClass = AMRCharacter::StaticClass();
	PlayerStateClass = AMRPlayerState::StaticClass();
	PlayerControllerClass = AMRPlayerController::StaticClass();
}

void AMRGameMode::RestartPlayer(AController* NewPlayer)
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (!NewPlayer || !Zones || !Zones->FindZone(StartZone))
	{
		Super::RestartPlayer(NewPlayer); // no zone data: fall back to PlayerStart actors
		return;
	}
	// TODO(persistence): use the character's saved zone and position from Supabase
	RestartPlayerAtTransform(NewPlayer, Zones->GetStartTransform(StartZone));
	if (AMRPlayerState* PS = NewPlayer->GetPlayerState<AMRPlayerState>())
	{
		PS->SetZoneId(StartZone);
	}

	if (!ZoneSmokeTest && UMRZoneSmokeTest::IsRequested() && NewPlayer->GetPawn())
	{
		ZoneSmokeTest = NewObject<UMRZoneSmokeTest>(this);
		ZoneSmokeTest->Start(NewPlayer->GetPawn());
	}
}
