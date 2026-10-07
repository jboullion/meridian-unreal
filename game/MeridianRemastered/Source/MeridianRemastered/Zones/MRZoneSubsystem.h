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
	/** Arrival point on the floor, zone-local UE cm, from zone_layout.json "teleport.pos". */
	UPROPERTY(BlueprintReadOnly) FVector TeleportLocal = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) bool bHasTeleportLocal = false;
	/** Another zone drawn from the same geometry (e.g. Raza town and its Outskirts), or 0. */
	UPROPERTY(BlueprintReadOnly) int32 SharesGeometryWith = 0;
	/** Zone whose streaming level holds this zone's geometry (itself unless it shares). */
	UPROPERTY(BlueprintReadOnly) int32 GeometryRid = 0;
	/** Short name of that streaming level, e.g. L_Zone_307_RazaBar: geometry rid and its Kod class (tools/ue/build_world.py). */
	UPROPERTY(BlueprintReadOnly) FName LevelName;
	UPROPERTY(BlueprintReadOnly) TArray<FMRZoneExit> Exits;
	UPROPERTY(BlueprintReadOnly) TArray<FMREdgeExit> EdgeExits;
	/** Zones reachable through one exit (tile or edge); the client keeps these preloaded. */
	UPROPERTY(BlueprintReadOnly) TArray<int32> Neighbours;

	double GridArea() const { return GridSizeRoo.X * GridSizeRoo.Y; }
};

class ULevelStreaming;

/**
 * Knows every zone's placement and exits, and moves players between zones on the server.
 *
 * Works like the original: exits are grid squares, and leaving a zone's grid rectangle
 * across an edge with an edge exit changes zone. When the destination shares geometry with
 * the current zone (the town and the Outskirts) only the zone ID changes, with no teleport,
 * so walking out of the north gate is seamless.
 *
 * Streaming: every zone's geometry is a streaming sublevel of L_World (L_Zone_<rid>_<KodClass>).
 *  - The server loads all of them at startup (authoritative collision, AI, traces).
 *  - Each client loads its current zone plus every zone one exit away and keeps them visible
 *    (AMRPlayerController drives this), so taking an exit is a same-frame switch.
 *  - The server only teleports or spawns a player into a zone the player's client has already
 *    made visible; otherwise it asks the client to load it and waits (up to a timeout), so a
 *    player can never land on geometry their client hasn't got.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRZoneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// --- streaming
	/** The streaming level holding a zone's geometry, or null (e.g. a map without zone sublevels). */
	ULevelStreaming* FindZoneLevel(int32 Rid) const;

	/** True if the zone's geometry is loaded and visible in this world. */
	bool IsZoneVisibleLocally(int32 Rid) const;

	/**
	 * Server: true if Controller can be put into the zone right now. For a remote player this
	 * means its client has reported the zone's level as visible; for anything else, that the
	 * level is visible on the server.
	 */
	bool IsZoneReadyFor(const AController* Controller, int32 Rid) const;

	/** Client: make exactly these zones' levels loaded and visible, unloading the rest. */
	void SetClientStreamingTarget(const TSet<int32>& ZoneRids);

	/** Server: load every zone level; bBlock waits until they are all in. */
	void LoadAllZoneLevels(bool bBlock);

	/** How long the server waits for a client to stream a destination before moving it anyway. */
	static constexpr double StreamWaitTimeoutSeconds = 8.0;

	bool IsLoaded() const { return Zones.Num() > 0; }
	const FMRZoneInfo* FindZone(int32 Rid) const { return Zones.Find(Rid); }
	const TMap<int32, FMRZoneInfo>& GetZones() const { return Zones; }

	/** World position of a grid square's centre; Z comes from a floor trace when geometry is loaded. */
	FVector GridToWorld(int32 Rid, int32 Row, int32 Col, bool bTraceFloor = true) const;

	/**
	 * Put InOut.Z on the floor under (X, Y): the highest surface with standing room above it. False if none.
	 * bLowest: the lowest such surface instead. The original rooms are 2.5D (floors never overlap), so
	 * anything with a floor below it is something we added on top: a roof, a canopy (monster spawns).
	 */
	bool TraceFloor(FVector& InOut, bool bLowest = false) const;

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

	/** Pawns waiting for their client to stream a teleport destination: (dest rid, wait start). */
	TMap<TWeakObjectPtr<APawn>, TPair<int32, double>> PendingTeleport;

	/** LevelName -> streaming level, filled on first lookup. */
	mutable TMap<FName, TWeakObjectPtr<ULevelStreaming>> LevelCache;
};
