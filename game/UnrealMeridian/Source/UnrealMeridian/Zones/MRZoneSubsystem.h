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
	/** The geometry's footprint in world XY (UE cm), from zone_layout.json "bounds_m" (the minimap's capture). */
	UPROPERTY(BlueprintReadOnly) FBox2D BoundsWorld = FBox2D(ForceInit);
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
	/**
	 * The security value of the .roo it was built from (zone_layout.json "roo_security", tools/roo2gltf).
	 * Online, the server's BP_PLAYER says which room it means; a mismatch means a different room.
	 */
	uint32 RooSecurity = 0;
	bool bHasRooSecurity = false;
	/**
	 * A runtime room's walls for the minimap (world XY, UE cm: x0, y0, x1, y1): its one-sided walls,
	 * as the original's map draws them (roo2gltf write_walls). Built zones use their captured picture.
	 */
	TArray<FVector4> MapWalls;

	double GridArea() const { return GridSizeRoo.X * GridSizeRoo.Y; }
};

/**
 * A wading area: a sector of the original with a depth (SF_MASK_DEPTH 1-3: fields, pools), one convex
 * BSP leaf of it in world XY (UE cm), from zone_layout.json "depth_areas" (tools/roo2gltf).
 */
struct FMRDepthArea
{
	int32 Depth = 0;
	/** The runtime zone it belongs to (UMRRuntimeRooms), 0 for the built zones. */
	int32 RuntimeRid = 0;
	FBox2D Bounds = FBox2D(ForceInit);
	TArray<FVector2D> Points;
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
class UNREALMERIDIAN_API UMRZoneSubsystem : public UWorldSubsystem
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

	/** True if Controller can be put into the zone right now: its level is in and visible. */
	bool IsZoneReadyFor(const AController* Controller, int32 Rid) const;

	/** Server: load every zone level; bBlock waits until they are all in. */
	void LoadAllZoneLevels(bool bBlock);

	/** How long a spawn or a teleport waits for its zone's level before going anyway. */
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

	/**
	 * The original's wading depth at a world position (0 none, 1-3 deeper): its fields and pools.
	 * Pure data, the same on client and server, so movement predicts it (UMRCharacterMovementComponent).
	 */
	int32 DepthAt(const FVector& World) const;

	/** How fast you move at a wading depth, as the original (clientd3d move.c): 1, 3/4, 1/2, 1/4. */
	static float DepthSpeedFactor(int32 Depth);

	/** World -> (row, col) in the given zone's grid. */
	FIntPoint WorldToGrid(int32 Rid, const FVector& World) const;

	/** The zone whose grid rectangle contains World; the smallest wins where they overlap. 0 if none. */
	int32 ZoneAtLocation(const FVector& World) const;

	/** Server: re-evaluate the pawn's zone, take edge exits. Called a few times a second. */
	void UpdatePawnZone(APawn* Pawn);

	/**
	 * Server: the original's "go" (space bar, BP_REQ_GO -> room.kod SomethingTryGo): take the tile
	 * exit (door) on the pawn's square. False if it stands on none, or on a locked one.
	 */
	bool TryGo(APawn* Pawn);

	/** Server: move the pawn into DestRid at (Row, Col), keeping its facing (ROTATE_NONE). */
	bool TeleportPawn(APawn* Pawn, int32 DestRid, int32 Row, int32 Col);

	/** Server: spawn point for new characters (the Raza Inn, settings.kod piInitialHomeRoomID). */
	FTransform GetStartTransform(int32 Rid = 301) const;

	// --- playing on a Meridian server (docs/adr/0010-meridian-servers.md)

	/** The zone built from a room file (zones.json "roo", e.g. "razainn.roo"; any case), or 0. */
	int32 RidForRoom(const FString& RooFile) const;

	/**
	 * A zone built at runtime from the server's .roo (UMRRuntimeRooms, docs/adr/0012): no streaming
	 * level, its geometry an AMRRuntimeRoom at Info.Origin. Depth areas are zone-local (UE cm, XY).
	 */
	void AddRuntimeZone(const FMRZoneInfo& Info, const TArray<FMRDepthArea>& LocalDepthAreas);
	void RemoveRuntimeZone(int32 Rid);

	/**
	 * A server position (Kod fine units: square * 64 + fine, 1-based, so 64 is the room's top-left
	 * edge) -> world, on the floor when bTraceFloor and the geometry is loaded.
	 */
	FVector KodToWorld(int32 Rid, int32 KodRow, int32 KodCol, bool bTraceFloor = true) const;

	/** World -> server position in a zone: X = Kod row, Y = Kod column (may fall outside the room). */
	FIntPoint WorldToKod(int32 Rid, const FVector& World) const;

	/** The server owns exits and zone changes: UpdatePawnZone leaves the pawn alone. */
	void SetServerDriven(bool bInServerDriven) { bServerDriven = bInServerDriven; }
	bool IsServerDriven() const { return bServerDriven; }

	/** Directory holding zones.json / zone_layout.json. */
	static FString GetDataDir();

	/** The prop gallery's zone (data/environment/prop_gallery.json), registered only with -MRGallery. */
	static constexpr int32 GalleryRid = 9000;

private:
	bool LoadData();
	void AddGalleryZone(const FString& Dir);
	int32 GetPawnZone(const APawn* Pawn) const;
	void SetPawnZone(APawn* Pawn, int32 Rid) const;

	TMap<int32, FMRZoneInfo> Zones;

	/** Room file (lower case) -> zone, from zones.json "roo". */
	TMap<FString, int32> RoomFiles;

	bool bServerDriven = false;

	/** Every zone geometry's wading areas, world space (DepthAt). */
	TArray<FMRDepthArea> DepthAreas;

	/** Pawns that just teleported don't take another exit until they leave the arrival square. */
	TMap<TWeakObjectPtr<APawn>, FIntVector> ArrivalSquare;

	/** Pawns waiting for their client to stream a teleport destination: (dest rid, wait start). */
	TMap<TWeakObjectPtr<APawn>, TPair<int32, double>> PendingTeleport;

	/** LevelName -> streaming level, filled on first lookup. */
	mutable TMap<FName, TWeakObjectPtr<ULevelStreaming>> LevelCache;
};
