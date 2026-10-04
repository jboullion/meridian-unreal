#include "Zones/MRZoneSubsystem.h"

#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

		const TSharedPtr<FJsonObject>* Shares = nullptr;
		if (L->TryGetObjectField(TEXT("shares_geometry_with"), Shares))
		{
			Info.SharesGeometryWith = IntField(*Shares, TEXT("rid"));
		}

		if (const TSharedPtr<FJsonObject>* KodPtr = KodZones.Find(Info.Rid))
		{
			const TSharedPtr<FJsonObject>& K = *KodPtr;
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
		if (const UWorld* World = GetWorld())
		{
			FHitResult Hit;
			const FVector Start = P + FVector(0, 0, 5000.0);
			const FVector End = P - FVector(0, 0, 5000.0);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MRGridToWorld), true);
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
			{
				P.Z = Hit.ImpactPoint.Z;
			}
		}
	}
	return P;
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
	if (!Pawn || !Pawn->HasAuthority() || Zones.Num() == 0)
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

	// Don't bounce straight back out through the exit next to the arrival point.
	if (const FIntVector* Arrived = ArrivalSquare.Find(Pawn))
	{
		if (*Arrived == Square)
		{
			return;
		}
		ArrivalSquare.Remove(Pawn);
	}

	// 1) tile exits (doors)
	for (const FMRZoneExit& E : Z.Exits)
	{
		if (E.Row == Grid.X && E.Col == Grid.Y)
		{
			if (E.bLocked)
			{
				// TODO(ui): show E.LockedMessage to the player once the chat/message UI exists
				return;
			}
			TeleportPawn(Pawn, E.DestRid, E.DestRow, E.DestCol);
			return;
		}
	}

	// 2) edge exits: left the grid rectangle on a side that leads somewhere
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

bool UMRZoneSubsystem::TeleportPawn(APawn* Pawn, int32 DestRid, int32 Row, int32 Col)
{
	if (!Pawn || !Pawn->HasAuthority() || !Zones.Contains(DestRid))
	{
		return false;
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

FTransform UMRZoneSubsystem::GetStartTransform(int32 Rid) const
{
	const FMRZoneInfo* Z = Zones.Find(Rid);
	if (!Z)
	{
		return FTransform::Identity;
	}
	FVector P = GridToWorld(Rid, Z->TeleportRow > 0 ? Z->TeleportRow : 5, Z->TeleportCol > 0 ? Z->TeleportCol : 5, true);
	P.Z += 100.0;
	return FTransform(FRotator(0.0, Z->TeleportYaw, 0.0), P);
}
