#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRRuntimeRooms.generated.h"

class AMRRuntimeRoom;
class FMRAssetCache;

/**
 * Rooms built at runtime from the server's own files (docs/adr/0012-client-parity-and-world-coverage.md,
 * the runtime tier): any room the server sends that we haven't built, or whose .roo differs from
 * the one ours was built from. Each becomes an AMRRuntimeRoom and a zone in UMRZoneSubsystem.
 *
 * - Files come through the asset cache (the .roo, then every grdNNNNN.bgf it uses).
 * - A runtime zone's RID is RuntimeRidBase + the room's Kod RID (data/net/rooms.json), so a server
 *   room that replaces one of our built zones never collides with it.
 * - Rooms sit in slots far north of the built zones; the last few stay built, so going back is instant.
 * - Prefetch downloads the rooms one exit away, so the next room builds from the disk cache.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRRuntimeRooms : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static constexpr int32 RuntimeRidBase = 100000;
	/** Rooms kept built (the current one and the ones just left). */
	static constexpr int32 MaxBuilt = 3;

	/** The zone, once it's built: Rid, or 0 if the room couldn't be built. */
	using FOnReady = TFunction<void(int32 Rid)>;

	/**
	 * Build (or reuse) the zone for a server room. Security is BP_PLAYER's (0: don't check). Done runs
	 * on the game thread when it's ready, possibly before this returns.
	 */
	void Request(const FString& RoomFile, const FString& RoomName, uint32 Security, FOnReady Done);

	/** The zone already built for a room file, or 0. */
	int32 FindBuiltRid(const FString& RoomFile) const;

	/** Download the files of the rooms one exit away from this one (data/net/rooms.json links). */
	void Prefetch(const FString& RoomFile);

	/** The room's Kod RID in data/net/rooms.json, or 0. */
	int32 KodRidForRoom(const FString& RoomFile) const;
	/** The .roo of a Kod RID, or empty. */
	FString RoomForKodRid(int32 KodRid) const;
	int32 NumIndexed() const { return Index.Num(); }

	/** Download a texture a built room was changed to (BP_CHANGE_TEXTURE) and give it to the room. */
	void FetchTexture(const FString& RoomFile, uint16 Texture);

	/** The actor of a built room (tests). */
	AMRRuntimeRoom* FindRoomActor(const FString& RoomFile) const;

private:
	struct FIndexEntry
	{
		int32 KodRid = 0;
		FString Name;
		TArray<int32> Links;
	};
	struct FBuilt
	{
		int32 Rid = 0;
		int32 Slot = 0;
		TWeakObjectPtr<AMRRuntimeRoom> Actor;
		double LastUsed = 0.0;
	};
	struct FPending;

	FMRAssetCache* Assets() const;
	void LoadIndex();
	void OnRoomBytes(TSharedPtr<FPending> Job, bool bOk, const TArray<uint8>& Bytes);
	void Finish(TSharedPtr<FPending> Job);
	void Fail(TSharedPtr<FPending> Job, const FString& Why);
	int32 TakeSlot();
	void Evict(const FString& Keep);

	/** room file (lower case) -> its index entry */
	TMap<FString, FIndexEntry> Index;
	TMap<int32, FString> RoomByKodRid;
	TMap<FString, FBuilt> Built;
	TMap<FString, TSharedPtr<FPending>> Pending;
	/** files asked for by Prefetch (each only once) */
	TSet<FString> Prefetched;
};
