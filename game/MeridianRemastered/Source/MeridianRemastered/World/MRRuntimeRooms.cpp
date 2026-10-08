#include "World/MRRuntimeRooms.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRAssetCache.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/MRBgf.h"
#include "World/MRRooFile.h"
#include "World/MRRoomMesh.h"
#include "World/MRRuntimeRoom.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	// runtime rooms sit in a row 16 km north of the built zones (which start at the origin and grow south-east)
	constexpr double SlotSpacingCm = 200000.0;
	constexpr double SlotRowYCm = -1600000.0;

	FString TextureFile(uint16 Texture)
	{
		return FString::Printf(TEXT("grd%05d.bgf"), Texture);
	}
}

struct UMRRuntimeRooms::FPending
{
	FString Roo;
	FString Name;
	uint32 Security = 0;
	TArray<FOnReady> Waiting;
	FMRRooFile Room;
	TMap<uint16, FMRBgf> Bgfs;
	int32 Outstanding = 0;
	double Started = 0.0;
};

bool UMRRuntimeRooms::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMRRuntimeRooms::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadIndex();
}

void UMRRuntimeRooms::Deinitialize()
{
	Pending.Reset();
	Built.Reset();
	Super::Deinitialize();
}

void UMRRuntimeRooms::LoadIndex()
{
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("net"), TEXT("rooms.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogMeridian, Warning, TEXT("World: no room index at %s (tools/kod_extract/extract.py)"), *Path);
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("rooms")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		FIndexEntry E;
		E.KodRid = static_cast<int32>(R->GetNumberField(TEXT("rid")));
		R->TryGetStringField(TEXT("name"), E.Name);
		for (const TSharedPtr<FJsonValue>& L : R->GetArrayField(TEXT("links")))
		{
			E.Links.Add(static_cast<int32>(L->AsNumber()));
		}
		const FString Roo = R->GetStringField(TEXT("roo")).ToLower();
		RoomByKodRid.Add(E.KodRid, Roo);
		// a room file some RIDs share (Tos and its variants): the first (lowest) RID names it
		if (!Index.Contains(Roo))
		{
			Index.Add(Roo, MoveTemp(E));
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("World: %d rooms in %s"), Index.Num(), *Path);
}

FMRAssetCache* UMRRuntimeRooms::Assets() const
{
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UMRNetSubsystem* Net = GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	return Net ? Net->GetAssets() : nullptr;
}

int32 UMRRuntimeRooms::KodRidForRoom(const FString& RoomFile) const
{
	const FIndexEntry* E = Index.Find(RoomFile.ToLower());
	return E ? E->KodRid : 0;
}

FString UMRRuntimeRooms::RoomForKodRid(int32 KodRid) const
{
	const FString* Roo = RoomByKodRid.Find(KodRid);
	return Roo ? *Roo : FString();
}

int32 UMRRuntimeRooms::FindBuiltRid(const FString& RoomFile) const
{
	const FBuilt* B = Built.Find(RoomFile.ToLower());
	return B && B->Actor.IsValid() ? B->Rid : 0;
}

AMRRuntimeRoom* UMRRuntimeRooms::FindRoomActor(const FString& RoomFile) const
{
	const FBuilt* B = Built.Find(RoomFile.ToLower());
	return B ? B->Actor.Get() : nullptr;
}

// ------------------------------------------------------------------------------ building

void UMRRuntimeRooms::Request(const FString& RoomFile, const FString& RoomName, uint32 Security, FOnReady Done)
{
	const FString Roo = RoomFile.ToLower();
	if (FBuilt* B = Built.Find(Roo); B && B->Actor.IsValid())
	{
		B->LastUsed = FPlatformTime::Seconds();
		Done(B->Rid);
		return;
	}
	if (TSharedPtr<FPending>* Running = Pending.Find(Roo))
	{
		(*Running)->Waiting.Add(MoveTemp(Done));
		return;
	}
	FMRAssetCache* Cache = Assets();
	if (!Cache || !Cache->IsListed(Roo))
	{
		UE_LOG(LogMeridian, Warning, TEXT("World: %s isn't among the server's game files; can't build it"), *Roo);
		Done(0);
		return;
	}
	TSharedPtr<FPending> Job = MakeShared<FPending>();
	Job->Roo = Roo;
	Job->Name = RoomName;
	Job->Security = Security;
	Job->Waiting.Add(MoveTemp(Done));
	Job->Started = FPlatformTime::Seconds();
	Pending.Add(Roo, Job);
	UE_LOG(LogMeridian, Log, TEXT("World: building %s (%s) from the server's files"), *Roo, *RoomName);
	TWeakObjectPtr<UMRRuntimeRooms> Weak(this);
	Cache->Fetch(Roo, [Weak, Job](bool bOk, const TArray<uint8>& Bytes)
	{
		if (UMRRuntimeRooms* Self = Weak.Get())
		{
			Self->OnRoomBytes(Job, bOk, Bytes);
		}
	});
}

void UMRRuntimeRooms::OnRoomBytes(TSharedPtr<FPending> Job, bool bOk, const TArray<uint8>& Bytes)
{
	FString Error;
	if (!bOk)
	{
		Fail(Job, TEXT("couldn't download it"));
		return;
	}
	if (!Job->Room.Load(Bytes, Error))
	{
		Fail(Job, Error);
		return;
	}
	if (Job->Security && !MRNetRead::SecurityMatches(Job->Room.Security, Job->Security))
	{
		// the assets site and the game server disagree: draw what we have, the server still decides where we are
		UE_LOG(LogMeridian, Warning, TEXT("World: %s from the assets site isn't the server's room (security %08x, server %08x)"),
			*Job->Roo, Job->Room.Security, Job->Security);
	}
	FMRAssetCache* Cache = Assets();
	TArray<uint16> Wanted;
	for (const uint16 T : Job->Room.TextureIds())
	{
		if (Cache && Cache->IsListed(TextureFile(T)))
		{
			Wanted.Add(T);
		}
		else
		{
			UE_LOG(LogMeridian, Verbose, TEXT("World: %s uses %s, which the server doesn't have"), *Job->Roo, *TextureFile(T));
		}
	}
	Job->Outstanding = Wanted.Num();
	if (Wanted.Num() == 0)
	{
		Finish(Job);
		return;
	}
	TWeakObjectPtr<UMRRuntimeRooms> Weak(this);
	for (const uint16 T : Wanted)
	{
		Cache->Fetch(TextureFile(T), [Weak, Job, T](bool bTexOk, const TArray<uint8>& TexBytes)
		{
			UMRRuntimeRooms* Self = Weak.Get();
			if (!Self)
			{
				return;
			}
			FMRBgf Bgf;
			FString TexError;
			if (bTexOk && Bgf.Load(TexBytes, TexError))
			{
				Job->Bgfs.Add(T, MoveTemp(Bgf));
			}
			else
			{
				UE_LOG(LogMeridian, Warning, TEXT("World: texture %s for %s: %s"), *TextureFile(T), *Job->Roo, bTexOk ? *TexError : TEXT("not downloaded"));
			}
			if (--Job->Outstanding == 0)
			{
				Self->Finish(Job);
			}
		});
	}
}

void UMRRuntimeRooms::Fail(TSharedPtr<FPending> Job, const FString& Why)
{
	UE_LOG(LogMeridian, Warning, TEXT("World: couldn't build %s: %s"), *Job->Roo, *Why);
	Pending.Remove(Job->Roo);
	for (FOnReady& Done : Job->Waiting)
	{
		Done(0);
	}
}

int32 UMRRuntimeRooms::TakeSlot()
{
	TSet<int32> Used;
	for (const TPair<FString, FBuilt>& Pair : Built)
	{
		Used.Add(Pair.Value.Slot);
	}
	int32 Slot = 0;
	while (Used.Contains(Slot))
	{
		++Slot;
	}
	return Slot;
}

void UMRRuntimeRooms::Evict(const FString& Keep)
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	while (Built.Num() >= MaxBuilt)
	{
		FString Oldest;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<FString, FBuilt>& Pair : Built)
		{
			if (Pair.Key != Keep && Pair.Value.LastUsed < OldestTime)
			{
				Oldest = Pair.Key;
				OldestTime = Pair.Value.LastUsed;
			}
		}
		if (Oldest.IsEmpty())
		{
			return;
		}
		const FBuilt Gone = Built.FindAndRemoveChecked(Oldest);
		if (AMRRuntimeRoom* A = Gone.Actor.Get())
		{
			A->Destroy();
		}
		if (Zones)
		{
			Zones->RemoveRuntimeZone(Gone.Rid);
		}
		UE_LOG(LogMeridian, Log, TEXT("World: dropped the runtime room %s (zone %d)"), *Oldest, Gone.Rid);
	}
}

void UMRRuntimeRooms::Finish(TSharedPtr<FPending> Job)
{
	Pending.Remove(Job->Roo);
	UWorld* World = GetWorld();
	UMRZoneSubsystem* Zones = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		Fail(Job, TEXT("no zones"));
		return;
	}
	const FMRRooFile& Room = Job->Room;
	auto Repeat = [Job](uint16 Texture)
	{
		const FMRBgf* B = Job->Bgfs.Find(Texture);
		return B ? B->TextureRepeatRoo() : FVector2D(MRRoo::RooPerSquare, MRRoo::RooPerSquare);
	};
	const FMRRoomMesh RenderMesh = MRRoomMesh::Build(Room, Repeat, false);
	const FMRRoomMesh CollisionMesh = MRRoomMesh::Build(Room, Repeat, true);
	TMap<uint16, UTexture2D*> Textures;
	for (const TPair<uint16, FMRBgf>& Pair : Job->Bgfs)
	{
		if (UTexture2D* Tex = Pair.Value.MakeTexture(0, true))
		{
			Textures.Add(Pair.Key, Tex);
		}
	}

	Evict(Job->Roo);
	const int32 Slot = TakeSlot();
	const FIndexEntry* Entry = Index.Find(Job->Roo);
	const int32 Rid = Entry ? RuntimeRidBase + Entry->KodRid : 2 * RuntimeRidBase + static_cast<int32>(GetTypeHash(Job->Roo) % RuntimeRidBase);
	const FVector Origin(Slot * SlotSpacingCm, SlotRowYCm, 0.0);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMRRuntimeRoom* Actor = World->SpawnActor<AMRRuntimeRoom>(AMRRuntimeRoom::StaticClass(), FTransform(Origin), Params);
	if (!Actor)
	{
		Fail(Job, TEXT("couldn't spawn its actor"));
		return;
	}
	Actor->SetRoomFile(Job->Roo);
	Actor->Build(RenderMesh, CollisionMesh, Textures, AMRRuntimeRoom::LoadMaterial());

	FMRZoneInfo Info;
	Info.Rid = Rid;
	Info.Name = !Job->Name.IsEmpty() ? Job->Name : (Entry ? Entry->Name : Job->Roo);
	Info.KodClass = TEXT("Runtime");
	Info.Origin = Origin;
	Info.GridSizeRoo = FVector2D(Room.Width, Room.Height);
	const FBox Bounds = RenderMesh.Bounds();
	if (Bounds.IsValid)
	{
		Info.BoundsWorld = FBox2D(FVector2D(Bounds.Min) + FVector2D(Origin), FVector2D(Bounds.Max) + FVector2D(Origin));
	}
	Info.GeometryRid = Rid;
	Info.RooSecurity = Room.Security;
	Info.bHasRooSecurity = true;
	if (Entry)
	{
		for (const int32 Link : Entry->Links)
		{
			if (Zones->FindZone(Link))
			{
				Info.Neighbours.Add(Link);  // a built zone next door stays loaded
			}
		}
	}
	TArray<FMRDepthArea> Depths;
	for (const FMRRoomDepthArea& A : MRRoomMesh::DepthAreas(Room))
	{
		FMRDepthArea& D = Depths.AddDefaulted_GetRef();
		D.Depth = A.Depth;
		D.Points = A.Points;
	}
	Zones->AddRuntimeZone(Info, Depths);

	FBuilt& B = Built.Add(Job->Roo);
	B.Rid = Rid;
	B.Slot = Slot;
	B.Actor = Actor;
	B.LastUsed = FPlatformTime::Seconds();
	UE_LOG(LogMeridian, Log, TEXT("World: %s is zone %d (slot %d), %d textures, ready in %.2f s"), *Job->Roo, Rid, Slot, Textures.Num(),
		FPlatformTime::Seconds() - Job->Started);
	for (FOnReady& Done : Job->Waiting)
	{
		Done(Rid);
	}
}

// ------------------------------------------------------------------------------ prefetch

void UMRRuntimeRooms::Prefetch(const FString& RoomFile)
{
	FMRAssetCache* Cache = Assets();
	const FIndexEntry* Entry = Index.Find(RoomFile.ToLower());
	const UMRZoneSubsystem* Zones = GetWorld() ? GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Cache || !Entry || !Zones)
	{
		return;
	}
	for (const int32 Link : Entry->Links)
	{
		const FString Roo = RoomForKodRid(Link);
		if (Roo.IsEmpty() || Zones->RidForRoom(Roo) || Built.Contains(Roo) || Prefetched.Contains(Roo) || !Cache->IsListed(Roo))
		{
			continue;  // built here, already built or fetched, or not on this server
		}
		Prefetched.Add(Roo);
		TWeakObjectPtr<UMRRuntimeRooms> Weak(this);
		Cache->Fetch(Roo, [Weak, Roo](bool bOk, const TArray<uint8>& Bytes)
		{
			UMRRuntimeRooms* Self = Weak.Get();
			FMRAssetCache* C = Self ? Self->Assets() : nullptr;
			FMRRooFile Room;
			FString Error;
			if (!C || !bOk || !Room.Load(Bytes, Error))
			{
				return;
			}
			for (const uint16 T : Room.TextureIds())
			{
				if (C->IsListed(TextureFile(T)))
				{
					C->Fetch(TextureFile(T), [](bool, const TArray<uint8>&) {});
				}
			}
			UE_LOG(LogMeridian, Verbose, TEXT("World: prefetched %s"), *Roo);
		});
	}
}
