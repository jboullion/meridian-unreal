#include "Game/MRGameMode.h"

#include "Character/MRCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "MeridianRemastered.h"
#include "Player/MRPlayerController.h"
#include "Player/MRPlayerState.h"
#include "Tests/MRZoneSmokeTest.h"
#include "TimerManager.h"
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
	// Setting the zone first makes the client start streaming it (and its neighbours).
	if (AMRPlayerState* PS = NewPlayer->GetPlayerState<AMRPlayerState>())
	{
		PS->SetZoneId(StartZone);
	}

	// Spawn only once the client has the start zone's geometry, so nobody drops through an
	// unloaded floor. Retry shortly; after the timeout spawn anyway.
	const double Now = FPlatformTime::Seconds();
	const double* WaitStart = SpawnWaitStart.Find(NewPlayer);
	if (!Zones->IsZoneReadyFor(NewPlayer, StartZone))
	{
		if (!WaitStart)
		{
			SpawnWaitStart.Add(NewPlayer, Now);
		}
		if (!WaitStart || Now - *WaitStart < UMRZoneSubsystem::StreamWaitTimeoutSeconds)
		{
			FTimerHandle Retry;
			TWeakObjectPtr<AController> WeakPlayer(NewPlayer);
			GetWorldTimerManager().SetTimer(Retry, FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]()
			{
				if (AController* P = WeakPlayer.Get(); P && !P->GetPawn())
				{
					RestartPlayer(P);
				}
			}), 0.1f, false);
			return;
		}
		UE_LOG(LogMeridian, Warning, TEXT("MRStreaming: client never streamed start zone %d; spawning anyway"), StartZone);
	}
	if (WaitStart)
	{
		UE_LOG(LogMeridian, Log, TEXT("MRStreaming: start zone %d ready on client after %.0f ms"), StartZone, (Now - *WaitStart) * 1000.0);
		SpawnWaitStart.Remove(NewPlayer);
	}
	RestartPlayerAtTransform(NewPlayer, Zones->GetStartTransform(StartZone));

	if (!ZoneSmokeTest && UMRZoneSmokeTest::IsRequested() && NewPlayer->GetPawn())
	{
		ZoneSmokeTest = NewObject<UMRZoneSmokeTest>(this);
		ZoneSmokeTest->Start(NewPlayer->GetPawn());
	}
}
