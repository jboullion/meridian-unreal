#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRMapCapture.generated.h"

class APlayerController;
class ASceneCapture2D;

/**
 * The minimap pictures (docs/adr/0009-user-interface.md), run with -MRMapCapture on a rendering
 * game (tools/ue/run_map_capture.ps1; a headless editor can't open L_World). For each geometry
 * zone: the player is moved to its arrival point (so the zone's lighting and mood apply) and
 * hidden, and an orthographic scene capture straight down, north up, over MRMinimap::CaptureRect
 * renders it. Interiors (minimap.json cut_height_cm) put the camera that far above the floor:
 * the ortho near plane is at the camera, so the ceilings above it don't hide the room. Writes
 * <repo>/build/minimap/raw/T_Map_<rid>.png and .json (the rectangle), then quits.
 * -MRMapCaptureOnly=300,301 limits the zones.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRMapCapture : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	void NextZone();
	void Capture();
	void After(float Seconds, void (UMRMapCapture::*Step)());

	TWeakObjectPtr<APlayerController> Controller;
	UPROPERTY()
	TObjectPtr<ASceneCapture2D> Camera;
	TArray<int32> Zones;
	int32 Index = -1;
	FString OutDir;
};
