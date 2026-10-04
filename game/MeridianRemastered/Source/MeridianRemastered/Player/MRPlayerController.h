#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MRPlayerController.generated.h"

/**
 * Also drives client-side zone streaming: on a network client, keeps the player's current zone
 * and every zone one exit away loaded and visible (UMRZoneSubsystem::SetClientStreamingTarget),
 * plus any zone the server asks for with ClientPrepareZone (e.g. a teleport to a far zone).
 */
UCLASS()
class MERIDIANREMASTERED_API AMRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMRPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Console: log the current view as a data/environment/lookdev_cameras.json entry. */
	UFUNCTION(Exec)
	void MRBookmark(const FString& Name);

	/** Server -> owning client: start streaming this zone now (it is about to be needed). */
	UFUNCTION(Client, Reliable)
	void ClientPrepareZone(int32 Rid);

protected:
	virtual void BeginPlay() override;

private:
	void UpdateZoneStreaming();

	/** Zones requested by the server, with the time the request expires. */
	TMap<int32, double> PreparedZones;

	/** Zones recently left, kept resident until the given time. */
	TMap<int32, double> RetainUntil;

	/** Last streaming target sent to the zone subsystem. */
	TSet<int32> StreamingTarget;

	/** Zone the local player was last seen in, and whether its level was visible on entry. */
	int32 LastZone = 0;

	/** Levels requested but not yet visible: zone -> request time, for load-time logging. */
	TMap<int32, double> PendingLoads;

	/** -MRScreenshots visual check. */
	UPROPERTY()
	TObjectPtr<class UMRScreenshotTour> ScreenshotTour;

	/** -MRProfile character cost measurement. */
	UPROPERTY()
	TObjectPtr<class UMRProfileTour> ProfileTour;

	/** -MRLookDev environment captures. */
	UPROPERTY()
	TObjectPtr<class UMRLookDevTour> LookDevTour;
};
