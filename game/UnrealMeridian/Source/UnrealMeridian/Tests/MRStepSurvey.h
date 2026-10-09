#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "World/MRRooFile.h"
#include "MRStepSurvey.generated.h"

class AMRRuntimeRoom;
class APlayerController;

/** A stand-in for the player in the step survey: its capsule and movement, no controller, no body. */
UCLASS(NotPlaceable)
class UNREALMERIDIAN_API AMRStepProbe : public ACharacter
{
	GENERATED_BODY()

public:
	AMRStepProbe(const FObjectInitializer& ObjectInitializer);
};

/**
 * Every step the original lets you take, tried in play (tools/ue/run_step_survey.ps1, -MRStepSurvey).
 * For each room it lists the crossings between two sectors that the original allows (clientd3d move.c:
 * a passable wall, a step up to 24 Kod units from where you stand unless the wall has no lower texture,
 * headroom of the player's height under an upper texture; and the server's room for you on the far side,
 * blakserv roofile.c) and that climb 10 cm or more or pass under less than 2 m, and walks a probe with
 * the player's capsule and movement across each, many at once.
 * Rooms: -MRStepSurvey (our built zones, their real collision and props), -MRStepSurvey=all (every
 * reference room: the others built at runtime from ReferenceServers/Server-104/resource/rooms), or a
 * list (-MRStepSurvey=ke1.roo,i3.roo). Writes Saved/MRStepSurvey/results.csv and logs
 * "MRStepSurvey: <room> n/m", the failures, and "MRStepSurvey: DONE n/m", then quits.
 */
UCLASS()
class UNREALMERIDIAN_API UMRStepSurvey : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bRunning; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

private:
	struct FCrossing
	{
		int32 Wall = 0;
		int32 From = 0, To = 0;          // sectors, 1-based
		FVector2D AtRoo = FVector2D::ZeroVector;
		FVector Start = FVector::ZeroVector;   // world, the probe's feet
		FVector2D Dir = FVector2D::ZeroVector; // world XY, across the wall
		FVector2D WallPoint = FVector2D::ZeroVector;  // world XY
		double ClimbCm = 0.0;            // collision floors, as we stand (wading lowered)
		double HeadCm = 0.0;             // room under the lowest ceiling on the way, cm (1e6: open sky)
		int32 DepthFrom = 0, DepthTo = 0;
		bool bSloped = false;
		bool bNoLowerTexture = false;
	};
	struct FRun
	{
		int32 Crossing = -1;
		double Time = 0.0;
	};

	bool BeginRoom();
	void EndRoom();
	void ListCrossings(const FMRRooFile& Room, const FVector& Origin);
	void Judge(int32 ProbeIndex, bool bTimedOut);
	void Finish();

	TWeakObjectPtr<APlayerController> Controller;
	TArray<FString> RoomFiles;
	int32 RoomIndex = -1;
	FString RoomFile;
	bool bRuntime = false;
	TWeakObjectPtr<AMRRuntimeRoom> RuntimeRoom;
	int32 RuntimeRid = 0;

	TArray<FCrossing> Crossings;
	int32 Next = 0;
	TArray<TWeakObjectPtr<AMRStepProbe>> Probes;
	TArray<FRun> Runs;

	int32 RoomTried = 0, RoomCrossed = 0, RoomSkipped = 0, RoomOverCap = 0;
	int32 Tried = 0, Crossed = 0, Skipped = 0, OverCap = 0;
	TArray<FString> Csv;
	double ReadyAt = 0.0;
	double Started = 0.0;
	bool bRunning = false;
};
