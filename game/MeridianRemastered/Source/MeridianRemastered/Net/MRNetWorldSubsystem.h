#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRNetWorldSubsystem.generated.h"

class AMRNetObject;
class APlayerController;
class UMRNetSubsystem;
struct FMRBgf;
struct FMRNetObject;
struct FMRNetProjectile;

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

	// --- targets (docs/adr/0012 M2; the original's gameuser.c UserTargetNextOrPrevious, SetUserTargetID)
	/** The chosen target (0: none). Our own id when targeting ourselves. */
	uint32 GetTargetId() const { return TargetId; }
	/** What the crosshair is on (0: nothing): the object nearest the middle of the view, in reach and in sight. */
	uint32 GetAimId() const { return AimId; }
	void SetTarget(uint32 Id);
	void ClearTarget() { SetTarget(0); }
	/** The original's next / previous target: the attackable objects in view, left to right. */
	void TargetNextOrPrevious(bool bNext);
	void TargetSelf();
	/** Take what the crosshair is on as the target (or clear it when it's on nothing). */
	void TargetAim();
	/** Look (BP_REQ_LOOK) at the target, else at what the crosshair is on. False: nothing to look at. */
	bool LookAtTarget();
	/** Every object at the crosshair, nearest the middle first (the Look picker when there are several). */
	TArray<uint32> ObjectsAtAim() const { return AimStack; }
	/** The things at the crosshair that can be picked up (OF_GETTABLE), nearest the middle first. */
	TArray<uint32> GettableAtAim() const;
	/**
	 * Work what the target or the crosshair is on, as the original's double click did (gameuser.c):
	 * a container is looked into (BP_SEND_OBJECT_CONTENTS), anything else that can be activated is
	 * (BP_REQ_ACTIVATE). False: nothing there to use.
	 */
	bool UseAim();
	// --- combat (docs/adr/0012 M4; the server decides every hit)
	/**
	 * The attack key, as the original's (gameuser.c UserAttackClosest): the target if it is in view,
	 * else what the crosshair is on if it can be attacked, else the nearest attackable thing in view
	 * within 5 squares (CLOSE_DISTANCE). Our position goes up first, so the server checks the range
	 * from where we are. At most one every 250 ms (ATTACK_DELAY); the server allows one a second.
	 * Returns what was attacked, 0 for nothing (a target out of view says so in the chat).
	 */
	uint32 Attack();
	/** The last thing attacked (the damage we deal is drawn over it). */
	uint32 GetLastAttackedId() const { return LastAttackedId; }
	// --- spells and items on targets (docs/adr/0012 M5)
	/**
	 * Cast a known spell (merintr spells.c SpellCast). One that takes no target goes straight away;
	 * else it goes at the target when it is in view (or is ourselves), else at what the crosshair is
	 * on; with neither, the next choice picks it (ChooseTarget: the attack click on the crosshair's
	 * object, \ for ourselves, a click on an item in the inventory). Returns true if it was sent now.
	 */
	bool CastSpell(uint32 SpellId);
	/**
	 * Use a carried item on something (BP_REQ_APPLY; gameuser.c StartApply): as the original, the
	 * next choice picks what it is used on.
	 */
	void ApplyItem(uint32 ItemId);
	/**
	 * Rest (R) or stand up, as the original's rest command (merintr command.c): the server stops our
	 * moves while we rest (PFLAG_NO_MOVE), so we don't walk, attack or cast meanwhile (mermain.c).
	 */
	void SetResting(bool bRest);
	bool IsResting() const { return bResting; }
	/** A spell or an item waits for its target; what the HUD says meanwhile ("Cast Bless on..."). */
	bool IsChoosingTarget() const { return PendingSpell != 0 || PendingItem != 0; }
	FString GetChoosingText() const;
	/** Give the waiting spell or item its target (0: what the crosshair is on). False if nothing waits or there is no target. */
	bool ChooseTarget(uint32 Id = 0);
	void CancelChoosing();

	/**
	 * The pawn's paralysis and the room's weather follow the server's effects (BP_EFFECT).
	 * bEntered: a room was just entered, so its weather was already falling there.
	 */
	void ApplyEffects(bool bEntered = false);

	/** Every object actor here (the name plates and the minimap draw them). */
	const TMap<uint32, TWeakObjectPtr<AMRNetObject>>& GetActors() const { return Actors; }
	/** How far names show and the crosshair reaches: the original's 15 squares (object3d.h MAX_NAME_DISTANCE). */
	static double NameDistanceCm();
	FSimpleMulticastDelegate OnTargetChanged;
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
	void OnEffect();
	void OnProjectile(const FMRNetProjectile& P);
	/** A one-off animation a BP_CHANGE carries (ANIMATE_ONCE: a swing, a cast, a bite) played on a sprite body. */
	void PlayServerAction(const FMRNetObject& O);
	/** The server's bitmap of a file (fetched through the asset cache, parsed once); Done runs with it, maybe later. */
	void FetchBgf(const FString& File, TFunction<void(TSharedPtr<const FMRBgf>)> Done);
	/** Where an object (or our pawn) stands: its feet. False if it isn't here. */
	bool FeetOf(uint32 Id, FVector& Out) const;
	/** Send where we stand now if it changed (before a go or an attack: the server checks from there). */
	void SendPositionNow();

	void SpawnObject(const FMRNetObject& Object);
	/** The world build's visible props here (built zones: the server's lamps, signs, tables as meshes). */
	void GatherProps();
	/** A prop standing on this floor point's square, and its top. */
	bool FindPropAt(const FVector& Floor, double& OutTop) const;
	/** An object with no sprite of ours gets the server's bitmap (fetched through the asset cache, parsed once). */
	void AttachBgfSprite(AMRNetObject* Actor, const FMRNetObject& Object);
	void ClearObjects();
	/** Put the pawn where the server says; bSameRoom (the room's data reloaded): only if it is far off. */
	void PlacePlayer(bool bSameRoom);
	/** Our pawn wears what the server says our character looks like (MRNetLook). */
	void ApplySelfLook();
	void ShowBackdrop();
	void SendMovement(double Now);
	void UpdateAim();
	/** In view of the camera and not behind a wall: where (in pixels) and how far. */
	bool IsInSight(const AMRNetObject* A, FVector2D& OutScreen, double& OutDistance) const;

	bool bActive = false;
	int32 Rid = 0;
	bool bRoomMatchesServer = true;
	FString LoadingRoom;
	uint32 TargetId = 0;
	uint32 AimId = 0;
	TArray<uint32> AimStack;
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
	TArray<TWeakObjectPtr<AActor>> Props;
	uint32 LastAttackedId = 0;
	uint32 PendingSpell = 0;
	bool bResting = false;
	/** Walking stops while a room loads, while paralyzed and while resting. */
	void UpdateFrozen();
	uint32 PendingItem = 0;
	double LastAttackTime = 0.0;
	double RoomEnteredTime = 0.0;
	TArray<TWeakObjectPtr<AActor>> Projectiles;
};
