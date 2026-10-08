#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRNetWorldSubsystem.generated.h"

class AMRNetObject;
class APlayerController;
class UMRNetSubsystem;
struct FMRBgf;
struct FMRNetObject;

/**
 * Puts a Meridian server's view of the world into this UE world (docs/adr/0010-meridian-servers.md).
 * Active in a standalone game that plays online (UMRNetSubsystem::WantsOnline).
 *
 * - Before a character is in the game: no pawn; the camera looks over Raza behind the login screen.
 * - A room from the server (BP_PLAYER + BP_ROOM_CONTENTS): its zone by room file, the player's pawn
 *   spawned or moved to the server's position, a sprite (AMRNetObject) for every creature in it.
 * - The pawn moves locally (the original's movement is client-side too); its position goes up as
 *   BP_REQ_MOVE every 250 ms while it changes, its facing as BP_REQ_TURN. Space sends BP_REQ_GO
 *   (RequestGo), as the original's space bar: the server takes the door the player stands on.
 *   Walking off the room's edge keeps asking to move off it (once a second), and the server answers
 *   with the next room. The server snapping the player back moves the pawn.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRNetWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	/** Playing on a server in this world. */
	bool IsActive() const { return bActive; }
	/** The zone of the server's current room (0 until one arrives or if we don't have it). */
	int32 GetRid() const { return Rid; }
	/** Our zone was built from the same .roo as the server's room (its security value matches). */
	bool DoesRoomMatchServer() const { return bRoomMatchesServer; }
	/** A room is being built from the server's files (UMRRuntimeRooms): its name, else empty. */
	const FString& GetLoadingRoom() const { return LoadingRoom; }
	/** The creature actor for a server object, if any. */
	AMRNetObject* FindActor(uint32 Id) const;
	/** The look a server object is drawn with (None: not drawn). */
	FName LookFor(const FMRNetObject& Object) const;

	/** Show the login screen or the HUD for the current phase. */
	void UpdateScreens();

	/**
	 * The original's "go" (space bar): send where the player stands, then BP_REQ_GO. The server
	 * (room.kod SomethingTryGo) takes the door on that square, or says it's locked.
	 */
	void RequestGo();

private:
	UMRNetSubsystem* GetNet() const;
	APlayerController* GetPC() const;

	void OnPhaseChanged();
	void OnRoomEntered();
	/** The room's zone is ready (built, or just made at runtime): place the pawn, spawn the objects. */
	void FinishEnterRoom(int32 PrevRid);
	/** No walking while the next room loads (the pawn stands in the old one). */
	void SetPawnFrozen(bool bFrozen);
	void OnObjectAdded(uint32 Id);
	void OnObjectChanged(uint32 Id);
	void OnObjectMoved(uint32 Id);
	void OnObjectRemoved(uint32 Id);

	void SpawnObject(const FMRNetObject& Object);
	/** An object with no sprite of ours gets the server's bitmap (fetched through the asset cache, parsed once). */
	void AttachBgfSprite(AMRNetObject* Actor, const FMRNetObject& Object);
	void ClearObjects();
	/** Put the pawn where the server says; bSameRoom (the room's data reloaded): only if it is far off. */
	void PlacePlayer(bool bSameRoom);
	/** Our pawn wears what the server says our character looks like (MRNetLook). */
	void ApplySelfLook();
	void ShowBackdrop();
	void SendMovement(double Now);

	bool bActive = false;
	int32 Rid = 0;
	bool bRoomMatchesServer = true;
	FString LoadingRoom;
	/** Bumped per room: a runtime room finishing after the server moved us on is ignored. */
	int32 RoomTicket = 0;
	TMap<uint32, TWeakObjectPtr<AMRNetObject>> Actors;

	FIntPoint LastSentKod = FIntPoint(-1, -1);
	double LastMoveTime = 0.0;
	double LastOffRoomTime = 0.0;
	int32 LastSentAngle = -1;
	double LastTurnTime = 0.0;
	/** bgf name (no extension, lower case) -> monster look, built on first use. */
	mutable TMap<FString, FName> LookByBgf;
	/** Parsed bitmaps of objects drawn from the server's files, by file name (lower case). */
	TMap<FString, TSharedPtr<const FMRBgf>> Bgfs;
};
