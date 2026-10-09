#include "Zones/MRZoneSubsystem.h"

#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/LevelStreaming.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Player/MRPlayerController.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonValue> LoadJson(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return nullptr;
		}
		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root))
		{
			return nullptr;
		}
		return Root;
	}

	int32 IntField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32 Default = 0)
	{
		double V = Default;
		return Obj.IsValid() && Obj->TryGetNumberField(Field, V) ? static_cast<int32>(V) : Default;
	}

	bool EdgeFromKod(const FString& Side, EMREdge& Out)
	{
		if (Side == TEXT("LEAVE_NORTH")) { Out = EMREdge::North; return true; }
		if (Side == TEXT("LEAVE_SOUTH")) { Out = EMREdge::South; return true; }
		if (Side == TEXT("LEAVE_EAST")) { Out = EMREdge::East; return true; }
		if (Side == TEXT("LEAVE_WEST")) { Out = EMREdge::West; return true; }
		return false;
	}
}

bool UMRZoneSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMRZoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!LoadData())
	{
		UE_LOG(LogMeridian, Error, TEXT("Zone data not found in %s (run tools/roo2gltf)"), *GetDataDir());
	}
}

void UMRZoneSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() != NM_Client)
	{
		// The server (dedicated, listen or standalone) simulates every zone.
		LoadAllZoneLevels(true);
	}
}

// ------------------------------------------------------------------------------ streaming

ULevelStreaming* UMRZoneSubsystem::FindZoneLevel(int32 Rid) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	const UWorld* World = GetWorld();
	if (!Z || !World)
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<ULevelStreaming>* Cached = LevelCache.Find(Z->LevelName))
	{
		if (Cached->IsValid())
		{
			return Cached->Get();
		}
	}
	const FString Wanted = Z->LevelName.ToString();
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming)
		{
			continue;
		}
		// PIE renames packages (UEDPIE_<n>_L_Zone_300); compare the plain short name
		const FString Package = UWorld::RemovePIEPrefix(Streaming->GetWorldAssetPackageFName().ToString());
		if (FPackageName::GetShortName(Package) == Wanted)
		{
			LevelCache.Add(Z->LevelName, Streaming);
			return Streaming;
		}
	}
	return nullptr;
}

bool UMRZoneSubsystem::IsZoneVisibleLocally(int32 Rid) const
{
	const ULevelStreaming* Streaming = FindZoneLevel(Rid);
	return !Streaming || Streaming->IsLevelVisible(); // no sublevel: geometry lives in the persistent level
}

bool UMRZoneSubsystem::IsZoneReadyFor(const AController* Controller, int32 Rid) const
{
	// this game runs one world, every zone loaded (LoadAllZoneLevels): ready once its level shows
	return IsZoneVisibleLocally(Rid);
}

void UMRZoneSubsystem::LoadAllZoneLevels(bool bBlock)
{
	UWorld* World = GetWorld();
	int32 Count = 0;
	TSet<FName> Seen;
	for (const TPair<int32, FMRZoneInfo>& Pair : Zones)
	{
		if (Seen.Contains(Pair.Value.LevelName))
		{
			continue;
		}
		Seen.Add(Pair.Value.LevelName);
		if (ULevelStreaming* Streaming = FindZoneLevel(Pair.Key))
		{
			Streaming->SetShouldBeLoaded(true);
			Streaming->SetShouldBeVisible(true);
			++Count;
		}
	}
	if (bBlock && World && Count > 0)
	{
		const double Start = FPlatformTime::Seconds();
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		UE_LOG(LogMeridian, Log, TEXT("Loaded %d zone levels in %.2f s"), Count, FPlatformTime::Seconds() - Start);
	}
	else if (Count == 0)
	{
		UE_LOG(LogMeridian, Log, TEXT("No zone streaming levels in this map; zone geometry is in the persistent level"));
	}
}

// -------------------------------------------------------------------------------- data

FString UMRZoneSubsystem::GetDataDir()
{
	// Packaged / synced copy first, then the repo's data/ folder during development.
	const FString Local = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"));
	if (IFileManager::Get().FileExists(*FPaths::Combine(Local, TEXT("zone_layout.json"))))
	{
		return Local;
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../data")));
}

bool UMRZoneSubsystem::LoadData()
{
	const FString Dir = GetDataDir();
	const TSharedPtr<FJsonValue> LayoutRoot = LoadJson(FPaths::Combine(Dir, TEXT("zone_layout.json")));
	const TSharedPtr<FJsonValue> ZonesRoot = LoadJson(FPaths::Combine(Dir, TEXT("zones.json")));
	if (!LayoutRoot.IsValid() || !ZonesRoot.IsValid())
	{
		return false;
	}

	// zones.json: flags, teleport point, tile exits, edge exits
	TMap<int32, TSharedPtr<FJsonObject>> KodZones;
	for (const TSharedPtr<FJsonValue>& V : ZonesRoot->AsArray())
	{
		const TSharedPtr<FJsonObject> Z = V->AsObject();
		KodZones.Add(IntField(Z, TEXT("rid")), Z);
	}

	for (const TSharedPtr<FJsonValue>& V : LayoutRoot->AsObject()->GetArrayField(TEXT("zones")))
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		FMRZoneInfo Info;
		Info.Rid = IntField(L, TEXT("rid"));
		Info.Name = L->GetStringField(TEXT("name"));
		Info.KodClass = L->GetStringField(TEXT("class"));

		const TArray<TSharedPtr<FJsonValue>>& O = L->GetArrayField(TEXT("world_origin_cm"));
		Info.Origin = FVector(O[0]->AsNumber(), O[1]->AsNumber(), O[2]->AsNumber());
		const TArray<TSharedPtr<FJsonValue>>& G = L->GetArrayField(TEXT("grid_size_roo"));
		Info.GridSizeRoo = FVector2D(G[0]->AsNumber(), G[1]->AsNumber());
		double Security = 0.0;
		if (L->TryGetNumberField(TEXT("roo_security"), Security))
		{
			Info.RooSecurity = static_cast<uint32>(static_cast<int64>(Security));
			Info.bHasRooSecurity = true;
		}
		const TSharedPtr<FJsonObject>* Bounds = nullptr;
		if (L->TryGetObjectField(TEXT("bounds_m"), Bounds))
		{
			const TArray<TSharedPtr<FJsonValue>>& Min = (*Bounds)->GetArrayField(TEXT("min"));
			const TArray<TSharedPtr<FJsonValue>>& Max = (*Bounds)->GetArrayField(TEXT("max"));
			if (Min.Num() == 3 && Max.Num() == 3)
			{
				const FVector A = Info.Origin + MRUnits::LayoutToLocal(Min[0]->AsNumber(), Min[1]->AsNumber(), Min[2]->AsNumber());
				const FVector B = Info.Origin + MRUnits::LayoutToLocal(Max[0]->AsNumber(), Max[1]->AsNumber(), Max[2]->AsNumber());
				Info.BoundsWorld = FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
			}
		}

		// roo2gltf's arrival point already sits on the floor (sector height at that spot)
		const TSharedPtr<FJsonObject>* LayoutTeleport = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* TeleportPos = nullptr;
		if (L->TryGetObjectField(TEXT("teleport"), LayoutTeleport) && (*LayoutTeleport)->TryGetArrayField(TEXT("pos"), TeleportPos)
			&& TeleportPos->Num() == 3)
		{
			Info.TeleportLocal = MRUnits::LayoutToLocal((*TeleportPos)[0]->AsNumber(), (*TeleportPos)[1]->AsNumber(), (*TeleportPos)[2]->AsNumber());
			Info.bHasTeleportLocal = true;
		}

		const TSharedPtr<FJsonObject>* Shares = nullptr;
		if (L->TryGetObjectField(TEXT("shares_geometry_with"), Shares))
		{
			Info.SharesGeometryWith = IntField(*Shares, TEXT("rid"));
		}
		// The layout records sharing on the zone that reuses another's geometry, so this is the
		// zone that owns the streaming level (matches build_world.py).
		Info.GeometryRid = Info.SharesGeometryWith ? Info.SharesGeometryWith : Info.Rid;  // LevelName: below

		// wading areas: only from the zone that owns the geometry (the Outskirts reuse the town's)
		const TArray<TSharedPtr<FJsonValue>>* Depths = nullptr;
		if (!Info.SharesGeometryWith && L->TryGetArrayField(TEXT("depth_areas"), Depths))
		{
			for (const TSharedPtr<FJsonValue>& DV : *Depths)
			{
				const TSharedPtr<FJsonObject> D = DV->AsObject();
				FMRDepthArea Area;
				Area.Depth = IntField(D, TEXT("depth"));
				for (const TSharedPtr<FJsonValue>& PV : D->GetArrayField(TEXT("points")))
				{
					const TArray<TSharedPtr<FJsonValue>>& XZ = PV->AsArray();
					const FVector W = Info.Origin + MRUnits::LayoutToLocal(XZ[0]->AsNumber(), 0.0, XZ[1]->AsNumber());
					Area.Points.Add(FVector2D(W.X, W.Y));
					Area.Bounds += Area.Points.Last();
				}
				if (Area.Depth > 0 && Area.Points.Num() >= 3)
				{
					DepthAreas.Add(MoveTemp(Area));
				}
			}
		}

		if (const TSharedPtr<FJsonObject>* KodPtr = KodZones.Find(Info.Rid))
		{
			const TSharedPtr<FJsonObject>& K = *KodPtr;
			FString Roo;
			if (K->TryGetStringField(TEXT("roo"), Roo) && !Roo.IsEmpty())
			{
				RoomFiles.Add(Roo.ToLower(), Info.Rid);
			}
			const TArray<TSharedPtr<FJsonValue>>* FlagArr = nullptr;
			if (K->TryGetArrayField(TEXT("flags"), FlagArr))
			{
				for (const TSharedPtr<FJsonValue>& F : *FlagArr)
				{
					Info.Flags.Add(F->AsString());
				}
			}
			Info.bNoCombat = Info.Flags.Contains(TEXT("ROOM_NO_COMBAT"));
			Info.bSanctuary = Info.Flags.Contains(TEXT("ROOM_SANCTUARY"));

			const TSharedPtr<FJsonObject> T = K->GetObjectField(TEXT("teleport"));
			Info.TeleportRow = IntField(T, TEXT("row"));
			Info.TeleportCol = IntField(T, TEXT("col"));
			Info.TeleportYaw = MRUnits::KodAngleToYaw(IntField(T, TEXT("angle")));

			for (const TSharedPtr<FJsonValue>& EV : K->GetArrayField(TEXT("exits")))
			{
				const TSharedPtr<FJsonObject> E = EV->AsObject();
				FMRZoneExit Exit;
				Exit.Row = IntField(E, TEXT("row"));
				Exit.Col = IntField(E, TEXT("col"));
				E->TryGetBoolField(TEXT("locked"), Exit.bLocked);
				E->TryGetStringField(TEXT("message"), Exit.LockedMessage);
				Exit.DestRid = IntField(E, TEXT("dest_rid"));
				Exit.DestRow = IntField(E, TEXT("dest_row"));
				Exit.DestCol = IntField(E, TEXT("dest_col"));
				Info.Exits.Add(Exit);
			}
			for (const TSharedPtr<FJsonValue>& EV : K->GetArrayField(TEXT("edge_exits")))
			{
				const TSharedPtr<FJsonObject> E = EV->AsObject();
				FMREdgeExit Edge;
				if (!EdgeFromKod(E->GetStringField(TEXT("side")), Edge.Edge))
				{
					continue;
				}
				Edge.DestRid = IntField(E, TEXT("dest_rid"));
				Edge.DestRow = IntField(E, TEXT("dest_row"));
				Edge.DestCol = IntField(E, TEXT("dest_col"));
				Info.EdgeExits.Add(Edge);
			}
		}
		Zones.Add(Info.Rid, MoveTemp(Info));
	}
	// the streaming level's name: L_Zone_<geometry rid>_<its Kod class>, e.g. L_Zone_307_RazaBar
	// (tools/ue/build_world.py zone_level_path); the Outskirts use the town's, L_Zone_300_Raza
	for (TPair<int32, FMRZoneInfo>& Pair : Zones)
	{
		const FMRZoneInfo* Geo = Zones.Find(Pair.Value.GeometryRid);
		Pair.Value.LevelName = FName(*FString::Printf(TEXT("L_Zone_%d_%s"), Pair.Value.GeometryRid,
			*(Geo ? Geo->KodClass : Pair.Value.KodClass)));
	}

	// zone_layout.json records sharing on one side only (the later zone); make it symmetric so
	// either side's edge exit into the other is a zone change rather than a teleport
	for (TPair<int32, FMRZoneInfo>& Pair : Zones)
	{
		const int32 Other = Pair.Value.SharesGeometryWith;
		if (Other && Zones.Contains(Other) && Zones[Other].SharesGeometryWith == 0)
		{
			Zones[Other].SharesGeometryWith = Pair.Key;
		}
	}

	// neighbour graph (only zones we actually have)
	for (TPair<int32, FMRZoneInfo>& Pair : Zones)
	{
		FMRZoneInfo& Z = Pair.Value;
		auto AddNeighbour = [this, &Z](int32 Rid)
		{
			if (Rid != Z.Rid && Zones.Contains(Rid))
			{
				Z.Neighbours.AddUnique(Rid);
			}
		};
		for (const FMRZoneExit& E : Z.Exits)
		{
			if (!E.bLocked)
			{
				AddNeighbour(E.DestRid);
			}
		}
		for (const FMREdgeExit& E : Z.EdgeExits)
		{
			AddNeighbour(E.DestRid);
		}
		AddNeighbour(Z.SharesGeometryWith);
	}

	UE_LOG(LogMeridian, Log, TEXT("Loaded %d zones from %s"), Zones.Num(), *Dir);
	return Zones.Num() > 0;
}

void UMRZoneSubsystem::AddRuntimeZone(const FMRZoneInfo& Info, const TArray<FMRDepthArea>& LocalDepthAreas)
{
	RemoveRuntimeZone(Info.Rid);
	Zones.Add(Info.Rid, Info);
	const FVector2D Origin(Info.Origin);
	for (const FMRDepthArea& Local : LocalDepthAreas)
	{
		FMRDepthArea& A = DepthAreas.Add_GetRef(Local);
		A.RuntimeRid = Info.Rid;
		A.Bounds = FBox2D(ForceInit);
		for (FVector2D& P : A.Points)
		{
			P += Origin;
			A.Bounds += P;
		}
	}
}

void UMRZoneSubsystem::RemoveRuntimeZone(int32 Rid)
{
	if (Zones.Remove(Rid) > 0)
	{
		DepthAreas.RemoveAll([Rid](const FMRDepthArea& A) { return A.RuntimeRid == Rid; });
	}
}

FVector UMRZoneSubsystem::GridToWorld(int32 Rid, int32 Row, int32 Col, bool bTraceFloor) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FVector::ZeroVector;
	}
	FVector P = Z->Origin + MRUnits::GridToLocal(Row, Col);
	if (bTraceFloor)
	{
		TraceFloor(P);
	}
	return P;
}

bool UMRZoneSubsystem::TraceFloor(FVector& P, bool bLowest) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Walk down from high above: the first surface with standing room above it is the floor.
	// Ceilings, roofs and rafters (hit from above) have another surface right over them.
	constexpr double HeadroomCm = 190.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRGridToWorld), true);
	// only the world: a creature's capsule isn't the floor (a corpse made where its monster still
	// stands landed on the monster's head), nor is it in the way of the headroom
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FVector Start = P + FVector(0, 0, 5000.0);
	const FVector End = P - FVector(0, 0, 5000.0);
	FHitResult Hit, Above;
	double Lowest = 0.0;
	bool bFound = false;
	for (int32 i = 0; i < (bLowest ? 16 : 8) && World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params, WorldOnly); ++i)
	{
		const FVector Floor = Hit.ImpactPoint;
		const bool bRoom = !World->LineTraceSingleByChannel(Above, Floor + FVector(0, 0, 5.0), Floor + FVector(0, 0, HeadroomCm),
			ECC_WorldStatic, Params, WorldOnly);
		UE_LOG(LogMeridian, Verbose, TEXT("TraceFloor (%.0f, %.0f): hit z=%.0f on %s / %s%s"), P.X, P.Y, Floor.Z,
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), bRoom ? TEXT("") : TEXT(" (no headroom, looking lower)"));
		P.Z = Floor.Z;
		if (const AActor* HitActor = Hit.GetActor(); HitActor && HitActor->ActorHasTag(TEXT("ZoneProp")))
		{
			Start = Floor - FVector(0, 0, 1.0);  // a solid prop (props.json "blocks": a table): the floor is under it
			continue;
		}
		if (bRoom && !bLowest)
		{
			return Hit.ImpactNormal.Z > 0.6;  // a floor, not a steep slope or a wall's top edge
		}
		if (bRoom && Hit.ImpactNormal.Z > 0.6)
		{
			Lowest = Floor.Z;
			bFound = true;
		}
		Start = Floor - FVector(0, 0, 1.0);
	}
	if (bFound)
	{
		P.Z = Lowest;
	}
	return bFound;  // false: no floor with standing room (P.Z: the last surface hit, if any)
}

int32 UMRZoneSubsystem::DepthAt(const FVector& World) const
{
	const FVector2D P(World.X, World.Y);
	for (const FMRDepthArea& Area : DepthAreas)
	{
		if (!Area.Bounds.IsInside(P))
		{
			continue;
		}
		// convex (a BSP leaf): inside when on the same side of every edge
		int32 Sign = 0;
		bool bInside = true;
		for (int32 i = 0, n = Area.Points.Num(); i < n && bInside; ++i)
		{
			const double C = FVector2D::CrossProduct(Area.Points[(i + 1) % n] - Area.Points[i], P - Area.Points[i]);
			const int32 S = C > 0.0 ? 1 : (C < 0.0 ? -1 : 0);
			if (S != 0)
			{
				bInside = Sign == 0 || S == Sign;
				Sign = Sign == 0 ? S : Sign;
			}
		}
		if (bInside)
		{
			return Area.Depth;
		}
	}
	return 0;
}

float UMRZoneSubsystem::DepthSpeedFactor(int32 Depth)
{
	switch (Depth)
	{
	case 1: return 0.75f;
	case 2: return 0.5f;
	case 3: return 0.25f;
	default: return 1.f;
	}
}

FIntPoint UMRZoneSubsystem::WorldToGrid(int32 Rid, const FVector& World) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FIntPoint::ZeroValue;
	}
	const FVector2D Roo = MRUnits::LocalToRoo(World - Z->Origin);
	// (row, col) are 1-based; FIntPoint is returned as (X = row, Y = col)
	return FIntPoint(FMath::FloorToInt(Roo.Y / MRUnits::RooPerSquare) + 1,
	                 FMath::FloorToInt(Roo.X / MRUnits::RooPerSquare) + 1);
}

int32 UMRZoneSubsystem::ZoneAtLocation(const FVector& World) const
{
	int32 Best = 0;
	double BestArea = TNumericLimits<double>::Max();
	for (const TPair<int32, FMRZoneInfo>& Pair : Zones)
	{
		const FMRZoneInfo& Z = Pair.Value;
		const FVector2D Roo = MRUnits::LocalToRoo(World - Z.Origin);
		const bool bInside = Roo.X >= 0 && Roo.Y >= 0 && Roo.X <= Z.GridSizeRoo.X && Roo.Y <= Z.GridSizeRoo.Y
			&& FMath::Abs(World.Z - Z.Origin.Z) < 50000.0;
		if (bInside && Z.GridArea() < BestArea)
		{
			Best = Z.Rid;
			BestArea = Z.GridArea();
		}
	}
	return Best;
}

int32 UMRZoneSubsystem::GetPawnZone(const APawn* Pawn) const
{
	const AMRPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMRPlayerState>() : nullptr;
	return PS ? PS->GetZoneId() : 0;
}

void UMRZoneSubsystem::SetPawnZone(APawn* Pawn, int32 Rid) const
{
	if (AMRPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMRPlayerState>() : nullptr)
	{
		if (PS->GetZoneId() != Rid)
		{
			UE_LOG(LogMeridian, Log, TEXT("%s: zone %d -> %d (%s)"), *PS->GetPlayerName(), PS->GetZoneId(), Rid,
				Zones.Contains(Rid) ? *Zones[Rid].Name : TEXT("?"));
			PS->SetZoneId(Rid);
		}
	}
}

void UMRZoneSubsystem::UpdatePawnZone(APawn* Pawn)
{
	if (!Pawn || !Pawn->HasAuthority() || Zones.Num() == 0 || bServerDriven)
	{
		return;
	}
	const FVector Pos = Pawn->GetActorLocation();
	int32 Current = GetPawnZone(Pawn);
	if (!Zones.Contains(Current))
	{
		Current = ZoneAtLocation(Pos);
		SetPawnZone(Pawn, Current);
		if (!Current)
		{
			return;
		}
	}
	// Moved far outside the current zone by something other than an exit (e.g. a GM teleport):
	// work out the zone from the position instead of treating it as an edge crossing.
	{
		const FMRZoneInfo& Cur = Zones[Current];
		const FVector2D R = MRUnits::LocalToRoo(Pos - Cur.Origin);
		const double Slack = 50.0 * MRUnits::RooPerSquare;
		if (R.X < -Slack || R.Y < -Slack || R.X > Cur.GridSizeRoo.X + Slack || R.Y > Cur.GridSizeRoo.Y + Slack)
		{
			if (const int32 Found = ZoneAtLocation(Pos))
			{
				SetPawnZone(Pawn, Found);
				Current = Found;
			}
		}
	}

	const FMRZoneInfo& Z = Zones[Current];
	const FIntPoint Grid = WorldToGrid(Current, Pos);
	const FIntVector Square(Current, Grid.X, Grid.Y);

	// A "go" through a door that is waiting for the client to stream the other side: keep trying
	// while the pawn stands on the door (TeleportPawn gives up waiting after a timeout).
	if (PendingTeleport.Contains(Pawn) && TryGo(Pawn))
	{
		return;
	}

	// Don't bounce straight back out through the exit next to the arrival point.
	if (const FIntVector* Arrived = ArrivalSquare.Find(Pawn))
	{
		if (*Arrived == Square)
		{
			return;
		}
		ArrivalSquare.Remove(Pawn);
	}

	// Tile exits (doors) wait for the player's "go" (TryGo), as in the original.
	// Edge exits: left the grid rectangle on a side that leads somewhere
	const FVector2D Roo = MRUnits::LocalToRoo(Pos - Z.Origin);
	for (const FMREdgeExit& E : Z.EdgeExits)
	{
		const bool bCrossed =
			(E.Edge == EMREdge::North && Roo.Y < 0.0) ||
			(E.Edge == EMREdge::South && Roo.Y > Z.GridSizeRoo.Y) ||
			(E.Edge == EMREdge::West && Roo.X < 0.0) ||
			(E.Edge == EMREdge::East && Roo.X > Z.GridSizeRoo.X);
		if (!bCrossed)
		{
			continue;
		}
		if (E.DestRid == Z.SharesGeometryWith)
		{
			SetPawnZone(Pawn, E.DestRid); // same physical space: just change zone
		}
		else if (Zones.Contains(E.DestRid))
		{
			TeleportPawn(Pawn, E.DestRid, E.DestRow, E.DestCol);
		}
		return;
	}
}

bool UMRZoneSubsystem::TryGo(APawn* Pawn)
{
	if (!Pawn || !Pawn->HasAuthority() || bServerDriven)
	{
		return false;
	}
	const int32 Current = GetPawnZone(Pawn);
	const FMRZoneInfo* Z = Zones.Find(Current);
	if (!Z)
	{
		return false;
	}
	const FIntPoint Grid = WorldToGrid(Current, Pawn->GetActorLocation());
	for (const FMRZoneExit& E : Z->Exits)
	{
		if (E.Row == Grid.X && E.Col == Grid.Y)
		{
			if (E.bLocked)
			{
				// TODO(ui): show E.LockedMessage to the player in the chat log
				UE_LOG(LogMeridian, Log, TEXT("Go: the door at zone %d (%d,%d) is locked"), Current, Grid.X, Grid.Y);
				return false;
			}
			return TeleportPawn(Pawn, E.DestRid, E.DestRow, E.DestCol);
		}
	}
	return false;
}

bool UMRZoneSubsystem::TeleportPawn(APawn* Pawn, int32 DestRid, int32 Row, int32 Col)
{
	if (!Pawn || !Pawn->HasAuthority() || !Zones.Contains(DestRid))
	{
		return false;
	}

	// Don't move a player onto geometry that isn't in yet (the zones still loading at start-up):
	// retry on later zone updates (the pawn is still standing on the exit), giving up after a timeout.
	AController* Controller = Pawn->GetController();
	if (Controller && !IsZoneReadyFor(Controller, DestRid))
	{
		const double Now = FPlatformTime::Seconds();
		TPair<int32, double>* Pending = PendingTeleport.Find(Pawn);
		if (!Pending || Pending->Key != DestRid)
		{
			PendingTeleport.Add(Pawn, TPair<int32, double>(DestRid, Now));
			UE_LOG(LogMeridian, Log, TEXT("MRStreaming: waiting for zone %d to load before the teleport"), DestRid);
			return false;
		}
		if (Now - Pending->Value < StreamWaitTimeoutSeconds)
		{
			return false;
		}
		UE_LOG(LogMeridian, Warning, TEXT("MRStreaming: zone %d didn't load within %.0f s; teleporting anyway"),
			DestRid, StreamWaitTimeoutSeconds);
	}
	if (PendingTeleport.Remove(Pawn) > 0)
	{
		UE_LOG(LogMeridian, Log, TEXT("MRStreaming: zone %d ready"), DestRid);
	}
	FVector Dest = GridToWorld(DestRid, Row, Col, true);
	Dest.Z += Pawn->GetSimpleCollisionHalfHeight() + 2.0;
	const bool bOk = Pawn->TeleportTo(Dest, Pawn->GetActorRotation(), false, true);
	if (bOk)
	{
		SetPawnZone(Pawn, DestRid);
		ArrivalSquare.Add(Pawn, FIntVector(DestRid, Row, Col));
	}
	else
	{
		UE_LOG(LogMeridian, Warning, TEXT("Teleport to zone %d (%d,%d) blocked"), DestRid, Row, Col);
	}
	return bOk;
}

int32 UMRZoneSubsystem::RidForRoom(const FString& RooFile) const
{
	const int32* Rid = RoomFiles.Find(FPaths::GetCleanFilename(RooFile).ToLower());
	return Rid ? *Rid : 0;
}

FVector UMRZoneSubsystem::KodToWorld(int32 Rid, int32 KodRow, int32 KodCol, bool bTraceFloor) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FVector::ZeroVector;
	}
	// Kod fine (64 per square, the room starting at 64) -> ROO (1024 per square, starting at 0)
	const double Scale = MRUnits::RooPerSquare / MRUnits::KodPerSquare;
	const FVector2D Roo((KodCol - MRUnits::KodPerSquare) * Scale, (KodRow - MRUnits::KodPerSquare) * Scale);
	FVector P = Z->Origin + MRUnits::RooToLocal(Roo);
	if (bTraceFloor)
	{
		TraceFloor(P);
	}
	return P;
}

FIntPoint UMRZoneSubsystem::WorldToKod(int32 Rid, const FVector& World) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FIntPoint::ZeroValue;
	}
	const FVector2D Roo = MRUnits::LocalToRoo(World - Z->Origin);
	const double Scale = MRUnits::KodPerSquare / MRUnits::RooPerSquare;
	return FIntPoint(FMath::FloorToInt(Roo.Y * Scale) + static_cast<int32>(MRUnits::KodPerSquare),
	                 FMath::FloorToInt(Roo.X * Scale) + static_cast<int32>(MRUnits::KodPerSquare));
}

FTransform UMRZoneSubsystem::GetStartTransform(int32 Rid) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FTransform::Identity;
	}
	// The arrival point's floor height is known from the room data; tracing for it can land on a
	// ceiling or anything else above the floor.
	FVector P = Z->bHasTeleportLocal
		? Z->Origin + Z->TeleportLocal
		: GridToWorld(Rid, Z->TeleportRow > 0 ? Z->TeleportRow : 5, Z->TeleportCol > 0 ? Z->TeleportCol : 5, true);
	P.Z += 100.0;
	return FTransform(FRotator(0.0, Z->TeleportYaw, 0.0), P);
}
