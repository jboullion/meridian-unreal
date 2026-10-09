#include "Game/MRGameMode.h"

#include "Character/MRCharacter.h"
#include "Engine/World.h"
#include "Game/MRGameState.h"
#include "GameFramework/Controller.h"
#include "UnrealMeridian.h"
#include "Misc/CommandLine.h"
#include "Net/MRNetSubsystem.h"
#include "Player/MRHUD.h"
#include "Player/MRPlayerController.h"
#include "Player/MRPlayerState.h"
#include "TimerManager.h"
#include "Zones/MRZoneSubsystem.h"

AMRGameMode::AMRGameMode()
{
	DefaultPawnClass = AMRCharacter::StaticClass();
	PlayerStateClass = AMRPlayerState::StaticClass();
	PlayerControllerClass = AMRPlayerController::StaticClass();
	GameStateClass = AMRGameState::StaticClass();
	HUDClass = AMRHUD::StaticClass();
}

void AMRGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	int32 Override = 0;
	if (FParse::Param(FCommandLine::Get(), TEXT("MRGallery")))
	{
		StartZone = UMRZoneSubsystem::GalleryRid;  // the prop gallery (UMRZoneSubsystem::AddGalleryZone)
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("MRStartZone="), Override) && Override > 0)
	{
		UE_LOG(LogMeridian, Log, TEXT("Start zone overridden: %d"), Override);
		StartZone = Override;
	}
}

void AMRGameMode::SpawnOnlinePlayer(AController* Player, const FTransform& At)
{
	if (Player && !Player->GetPawn())
	{
		RestartPlayerAtTransform(Player, At);
	}
}

void AMRGameMode::RestartPlayer(AController* NewPlayer)
{
	if (UMRNetSubsystem::WantsOnline(GetWorld()))
	{
		return;  // no pawn before the server has the character in a room (SpawnOnlinePlayer)
	}
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

	// Spawn only once the start zone's geometry is in, so nobody drops through an unloaded floor.
	// Retry shortly; after the timeout spawn anyway.
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
		UE_LOG(LogMeridian, Warning, TEXT("MRStreaming: start zone %d never loaded; spawning anyway"), StartZone);
	}
	if (WaitStart)
	{
		UE_LOG(LogMeridian, Log, TEXT("MRStreaming: start zone %d ready after %.0f ms"), StartZone, (Now - *WaitStart) * 1000.0);
		SpawnWaitStart.Remove(NewPlayer);
	}
	RestartPlayerAtTransform(NewPlayer, Zones->GetStartTransform(StartZone));
	if (!NewPlayer->GetPawn())
	{
		// never leave a player without a character (they'd be stuck with no input): try again shortly
		UE_LOG(LogMeridian, Warning, TEXT("Spawn in zone %d failed; retrying"), StartZone);
		FTimerHandle Retry;
		TWeakObjectPtr<AController> WeakPlayer(NewPlayer);
		GetWorldTimerManager().SetTimer(Retry, FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]()
		{
			if (AController* P = WeakPlayer.Get(); P && !P->GetPawn())
			{
				RestartPlayer(P);
			}
		}), 0.25f, false);
		return;
	}
}
