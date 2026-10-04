#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRZoneSubsystem.generated.h"

class APawn;

/** A tile exit: standing on (Row, Col) moves you to DestRid at (DestRow, DestCol). */
USTRUCT(BlueprintType)
struct FMRZoneExit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Row = 0;
	UPROPERTY(BlueprintReadOnly) int32 Col = 0;
	UPROPERTY(BlueprintReadOnly) bool bLocked = false;
	UPROPERTY(BlueprintReadOnly) FString LockedMessage;
	UPROPERTY(BlueprintReadOnly) int32 DestRid = 0;
	UPROPERTY(BlueprintReadOnly) int32 DestRow = 0;
	UPROPERTY(BlueprintReadOnly) int32 DestCol = 0;
};

UENUM(BlueprintType)
enum class EMREdge : uint8 { North, South, East, West };

/** Leaving the zone's grid rectangle across Edge moves you to DestRid. */
USTRUCT(BlueprintType)
struct FMREdgeExit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) EMREdge Edge = EMREdge::North;
	UPROPERTY(BlueprintReadOnly) int32 DestRid = 0;
	UPROPERTY(BlueprintReadOnly) int32 DestRow = 0;
	UPROPERTY(BlueprintReadOnly) int32 DestCol = 0;
};

/** One original room (zone), as described by data/zones.json + data/zone_layout.json. */
USTRUCT(BlueprintType)
struct FMRZoneInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Rid = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString KodClass;
	/** World position of the zone's ROO origin (UE cm). */
	UPROPERTY(BlueprintReadOnly) FVector Origin = FVector::ZeroVector;
	/** Kod grid rectangle size in ROO units: [0, X] x [0, Y] in zone coordinates. */
	UPROPERTY(BlueprintReadOnly) FVector2D GridSizeRoo = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) TArray<FString> Flags;
	UPROPERTY(BlueprintReadOnly) bool bNoCombat = false;
	UPROPERTY(BlueprintReadOnly) bool bSanctuary = false;
	UPROPERTY(BlueprintReadOnly) int32 TeleportRow = 0;
	UPROPERTY(BlueprintReadOnly) int32 TeleportCol = 0;
	UPROPERTY(BlueprintReadOnly) float TeleportYaw = 0.f;
	/** Another zone drawn from the same geometry (e.g. Raza town and its Outskirts), or 0. */
	UPROPERTY(BlueprintReadOnly) int32 SharesGeometryWith = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FMRZoneExit> Exits;
	UPROPERTY(BlueprintReadOnly) TArray<FMREdgeExit> EdgeExits;
	/** Zones reachable through one exit (tile or edge); the client keeps these preloaded. */
	UPROPERTY(BlueprintReadOnly) TArray<int32> Neighbours;

	double GridArea() const { return GridSizeRoo.X * GridSizeRoo.Y; }
};

/**
 * Knows every zone's placement and exits, and moves players between zones on the server.
 *
 * Works like the original: exits are grid squares, and leaving a zone's grid rectangle
 * across an edge with an edge exit changes zone. When the destination shares geometry with
 * the current zone (the town and the Outskirts) only the zone ID changes, with no teleport,
 * so walking out of the north gate is seamless.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRZoneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool IsLoaded() const { return Zones.Num() > 0; }
	const FMRZoneInfo* FindZone(int32 Rid) const { return Zones.Find(Rid); }
	const TMap<int32, FMRZoneInfo>& GetZones() const { return Zones; }

	/** World position of a grid square's centre; Z comes from a floor trace when geometry is loaded. */
	FVector GridToWorld(int32 Rid, int32 Row, int32 Col, bool bTraceFloor = true) const;

	/** World -> (row, col) in the given zone's grid. */
	FIntPoint WorldToGrid(int32 Rid, const FVector& World) const;

	/** The zone whose grid rectangle contains World; the smallest wins where they overlap. 0 if none. */
	int32 ZoneAtLocation(const FVector& World) const;

	/** Server: re-evaluate the pawn's zone, take tile/edge exits. Called a few times a second. */
	void UpdatePawnZone(APawn* Pawn);

	/** Server: move the pawn into DestRid at (Row, Col), keeping its facing (ROTATE_NONE). */
	bool TeleportPawn(APawn* Pawn, int32 DestRid, int32 Row, int32 Col);

	/** Server: spawn point for new characters (the Raza Inn, settings.kod piInitialHomeRoomID). */
	FTransform GetStartTransform(int32 Rid = 301) const;

	/** Directory holding zones.json / zone_layout.json. */
	static FString GetDataDir();

private:
	bool LoadData();
	int32 GetPawnZone(const APawn* Pawn) const;
	void SetPawnZone(APawn* Pawn, int32 Rid) const;

	TMap<int32, FMRZoneInfo> Zones;

	/** Pawns that just teleported don't take another exit until they leave the arrival square. */
	TMap<TWeakObjectPtr<APawn>, FIntVector> ArrivalSquare;
};
