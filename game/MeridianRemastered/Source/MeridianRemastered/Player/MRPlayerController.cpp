#include "Player/MRPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "MeridianRemastered.h"
#include "Player/MRPlayerState.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRSpriteNetTest.h"
#include "Tests/MRMonsterTour.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	/** How long a server ClientPrepareZone request keeps a zone streamed in. */
	constexpr double PrepareZoneSeconds = 15.0;

	/** How long a zone stays resident after it stops being the current zone or a neighbour. */
	constexpr double RetainSeconds = 30.0;
}

AMRPlayerController::AMRPlayerController()
{
}

void AMRPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		if (UMRScreenshotTour::IsRequested())
		{
			ScreenshotTour = NewObject<UMRScreenshotTour>(this);
			ScreenshotTour->Start(this);
		}
		else if (UMRProfileTour::IsRequested())
		{
			ProfileTour = NewObject<UMRProfileTour>(this);
			ProfileTour->Start(this);
		}
		else if (UMRMonsterTour::IsRequested())
		{
			MonsterTour = NewObject<UMRMonsterTour>(this);
			MonsterTour->Start(this);
		}
		else if (UMRSpriteNetTest::IsRequested())
		{
			SpriteNetTest = NewObject<UMRSpriteNetTest>(this);
			SpriteNetTest->Start(this);
		}
		else if (UMRSpriteClipTour::IsRequested())
		{
			SpriteClipTour = NewObject<UMRSpriteClipTour>(this);
			SpriteClipTour->Start(this);
		}
		else if (UMRLookDevTour::IsRequested())
		{
			LookDevTour = NewObject<UMRLookDevTour>(this);
			LookDevTour->Start(this);
		}
	}
}

void AMRPlayerController::MRBookmark(const FString& Name)
{
	FVector Location;
	FRotator Rotation;
	GetPlayerViewPoint(Location, Rotation);
	const float Fov = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 90.f;
	const FString Entry = UMRLookDevTour::FormatBookmark(Name.IsEmpty() ? TEXT("bookmark") : Name, Location, Rotation, Fov);
	UE_LOG(LogMeridian, Display, TEXT("MRBookmark: %s"), *Entry);
	ClientMessage(Entry);
}

void AMRPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// The server (dedicated, listen or standalone) keeps every zone loaded itself.
	if (IsLocalController() && GetWorld()->GetNetMode() == NM_Client)
	{
		UpdateZoneStreaming();
	}
}

void AMRPlayerController::ClientPrepareZone_Implementation(int32 Rid)
{
	PreparedZones.Add(Rid, FPlatformTime::Seconds() + PrepareZoneSeconds);
	UE_LOG(LogMeridian, Log, TEXT("MRStreaming: server asked to prepare zone %d"), Rid);
	UpdateZoneStreaming();
}

void AMRPlayerController::UpdateZoneStreaming()
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const AMRPlayerState* PS = GetPlayerState<AMRPlayerState>();
	if (!Zones || !Zones->IsLoaded())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const int32 Current = PS ? PS->GetZoneId() : 0;

	// what should be resident: current zone, its neighbours, and live server requests
	TSet<int32> Target;
	if (const FMRZoneInfo* Z = Zones->FindZone(Current))
	{
		Target.Add(Current);
		Target.Append(Z->Neighbours);
	}
	for (auto It = PreparedZones.CreateIterator(); It; ++It)
	{
		if (It->Value < Now)
		{
			It.RemoveCurrent();
		}
		else
		{
			Target.Add(It->Key);
		}
	}
	// Zones that just dropped out of the set stay resident for a while, so popping into a shop
	// and straight back out doesn't unload and reload the rest of the town.
	const TSet<int32> Core = Target;
	for (const int32 Rid : StreamingTarget)
	{
		if (!Core.Contains(Rid) && !RetainUntil.Contains(Rid))
		{
			RetainUntil.Add(Rid, Now + RetainSeconds);
		}
	}
	for (auto It = RetainUntil.CreateIterator(); It; ++It)
	{
		if (Core.Contains(It->Key) || It->Value < Now)
		{
			It.RemoveCurrent();
		}
		else
		{
			Target.Add(It->Key);
		}
	}

	if (!Target.Difference(StreamingTarget).IsEmpty() || !StreamingTarget.Difference(Target).IsEmpty())
	{
		for (const int32 Rid : Target)
		{
			if (!StreamingTarget.Contains(Rid) && !Zones->IsZoneVisibleLocally(Rid))
			{
				PendingLoads.Add(Rid, Now);
			}
		}
		StreamingTarget = Target;
		Zones->SetClientStreamingTarget(Target);
	}

	// load-time logging
	for (auto It = PendingLoads.CreateIterator(); It; ++It)
	{
		if (Zones->IsZoneVisibleLocally(It->Key))
		{
			UE_LOG(LogMeridian, Log, TEXT("MRStreaming: zone %d visible after %.0f ms"), It->Key, (Now - It->Value) * 1000.0);
			It.RemoveCurrent();
		}
		else if (!Target.Contains(It->Key))
		{
			It.RemoveCurrent();
		}
	}

	// Entering a zone: was its geometry already there? (It should be: it was a preloaded
	// neighbour, or the server waited for it.) "ready=0" here means a visible hitch.
	// Only counts once the pawn exists: the server sets the start zone before spawning and
	// holds the spawn until the client has streamed it.
	if (Current != LastZone && Current != 0 && GetPawn())
	{
		UE_LOG(LogMeridian, Log, TEXT("MRStreaming: entered zone %d, ready=%d"), Current, Zones->IsZoneVisibleLocally(Current) ? 1 : 0);
		LastZone = Current;
	}
}
