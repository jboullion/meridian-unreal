#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Net/MRCharInfo.h"
#include "Net/MRNetWorld.h"
#include "Net/MRResources.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MRNetSubsystem.generated.h"

class FMRAssetCache;
class FMRConnection;
class FMRReader;
class UWorld;

/** One Meridian server from data/net/servers.json. */
struct FMRServerEntry
{
	FString Name;
	/** The gateway's WebSocket URL (ws:// or wss://). */
	FString Ws;
	/** Where the server's game files are served (manifest.json, rsc0000.rsb). */
	FString Assets;
	/** blakserv's [Login] SecretKey. Not a secret: every client of that server ships it. */
	FString SecretKey;
	/** Origin header for the gateway's allowlist. */
	FString Origin;
	/** Which rules it runs ("server104"): how the UI lays out what it sends (data/ui/stat_layout.json). */
	FString Ruleset;
};

/** A character on the account (BP_CHARACTERS). */
struct FMRCharacterSlot
{
	uint32 Id = 0;
	FString Name;
	/** An empty slot: it has to go through character creation first. */
	bool bNeedsCreation = false;
};

struct FMRChatLine
{
	FString Text;
	uint8 Kind = 0;        // MRMsg::SAY_* for speech, 0 for game messages
	double Time = 0.0;
};

enum class EMRNetPhase : uint8
{
	Offline,      // not connected (the login screen)
	Connecting,   // fetching game data, connecting, logging in
	Characters,   // choosing a character
	Creating,     // the character creator (waiting for the server's options, editing, submitting)
	Entering,     // waiting for the first room
	InGame,
};

DECLARE_MULTICAST_DELEGATE(FOnMRNetEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetObjectEvent, uint32 /* Id */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetChat, const FMRChatLine&);

/**
 * The session with a Meridian server (docs/adr/0010-meridian-servers.md): the server list, the
 * server's game data, login, character select and creation, and the state of the player's room.
 * The state lives in an FMRNetWorld (Net/MRNetWorld.h); UMRNetWorldSubsystem puts it into the UE
 * world, and the login screen, HUD and chat log read it. The server's other game files (rooms,
 * bitmaps, sounds) come through the asset cache (Net/MRAssetCache.h).
 *
 * The protocol is ours (Net/MRProtocol, written from blakserv); never port Shards (GPLv2) code here.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * True when this world should play on a server: a standalone game (PIE single player too) that
	 * isn't running an offline test tour or -MROffline. Multiplayer PIE and the dedicated server keep
	 * the old UE-server path until it is retired.
	 */
	static bool WantsOnline(const UWorld* World);
	/** -MROffline or one of the visual test tours (they need a local pawn). */
	static bool IsOfflineRequested();

	// --- servers
	const TArray<FMRServerEntry>& GetServers() const { return Servers; }
	int32 GetLastServer() const { return LastServer; }
	const FString& GetLastUser() const { return LastUser; }

	// --- session
	void Connect(int32 ServerIndex, const FString& User, const FString& Password);
	/** Leave the server (or stop connecting). */
	void Logoff();
	/**
	 * Leave the game for the character list, staying connected: BP_REQ_QUIT; the server answers
	 * BP_QUIT and puts the session back at its menu, and we ask for the game again (FMRConnection).
	 */
	void ReturnToCharacters();
	/**
	 * Forget the room and ask for the player, room, players and stats again: what BP_INVALIDATE_DATA
	 * does after a server save (clientd3d game.c ResetUserData). Also a test hook.
	 */
	void ReloadData();
	void UseCharacter(uint32 Id);
	/** Open the creator for an empty slot: asks the server what it offers (BP_CHARINFO, OnCharInfo). */
	void RequestCharInfo(uint32 SlotId);
	/** Send the new character (Phase Creating). The server answers OK (then we enter) or not (LastError). */
	void CreateCharacter(const FMRNewCharacter& Character);
	/** Leave the creator for the character list (nothing is sent: asking for the options changed nothing). */
	void CancelCreation();
	/** What the server offers a new character (valid once OnCharInfo fired). */
	const FMRCharInfo& GetCharInfo() const { return CharInfo; }
	uint32 GetCreateSlot() const { return CreateSlot; }
	/** True while a new character waits for the server's answer. */
	bool IsSubmittingCharacter() const { return bSubmittingCharacter; }
	/** data/charinfo.json (from Kod): the creator's options without a server. */
	static bool LoadMockCharInfo(FMRCharInfo& Out);

	// --- in game
	void RequestMove(int32 KodRow, int32 KodCol, uint8 Speed);
	void RequestTurn(int32 KodAngle);
	void RequestGo();
	void Say(const FString& Text);
	/** Ask for an object's (or a player's) description: BP_REQ_LOOK; the answer fires OnDescription. */
	void RequestLook(uint32 ObjectId);
	/** Write a description (one's own: BP_CHANGE_DESCRIPTION, up to 1000 characters). */
	void ChangeDescription(uint32 ObjectId, const FString& Text);
	/** The last description the server sent (OnDescription). */
	const FMRNetDescription& GetDescription() const { return Description; }

	// --- state
	EMRNetPhase GetPhase() const { return Phase; }
	const FString& GetStatus() const { return Status; }
	const FString& GetLastError() const { return LastError; }
	const TArray<FMRCharacterSlot>& GetCharacters() const { return Characters; }
	const FString& GetMotd() const { return Motd; }
	const FMRNetWorld& GetNetWorld() const { return World; }
	const FMRNetPlayer& GetPlayer() const { return World.Player; }
	const TMap<uint32, FMRNetObject>& GetObjects() const { return World.Objects; }
	const FMRNetObject* FindObject(uint32 Id) const { return World.Objects.Find(Id); }
	const FMRNetObject* GetSelf() const { return World.Objects.Find(World.Player.Id); }
	/** Who is logged on (BP_PLAYERS), by object id. */
	const TMap<uint32, FMRNetUser>& GetUsers() const { return World.Users; }
	/**
	 * The server is saving (BP_WAIT .. BP_UNWAIT), or our data is stale and being asked for again
	 * (BP_INVALIDATE_DATA, a resync): object ids may change, so nothing should name one meanwhile.
	 */
	bool IsWaiting() const { return bWaiting || bAwaitingRoom; }
	/** Rooms entered since connecting (also the same room again after the data was reloaded). */
	int32 GetRoomsEntered() const { return RoomsEntered; }
	/** The server's game files (rooms, bitmaps, sounds), or null before connecting. */
	FMRAssetCache* GetAssets() const { return Assets.Get(); }
	const TArray<FMRChatLine>& GetChat() const { return Chat; }
	const FMRResourceTable& GetResources() const { return Resources; }
	/** The player's stat groups (empty until the server has sent them, after entering the game). */
	const TArray<FMRNetStatGroup>& GetStatGroups() const { return World.StatGroups; }
	/** A group by the server's number (Server 104: 1 condition, 2 stats, 3 spells, 4 skills, 5 quests). */
	const FMRNetStatGroup* FindStatGroup(uint8 Group) const;
	const FMRServerEntry* GetServer() const { return Servers.IsValidIndex(ServerIndex) ? &Servers[ServerIndex] : nullptr; }

	/** Tests and UI shots: show a character list without a server. */
	void SetPreview(EMRNetPhase InPhase, const TArray<FMRCharacterSlot>& InCharacters, const FString& InError = FString());
	/** Tests and UI shots: the creator with these options, without a server. */
	void SetPreviewCharInfo(const FMRCharInfo& Info, uint32 SlotId);
	/** What the login screen says while it waits (e.g. a room this client hasn't built). */
	void SetStatus(const FString& InStatus) { Status = InStatus; }

	FOnMRNetEvent OnPhaseChanged;
	FOnMRNetEvent OnCharactersChanged;
	/** BP_CHARINFO arrived: the creator can open. */
	FOnMRNetEvent OnCharInfo;
	/** The server refused the new character (BP_CHARINFO_NOT_OK): LastError says so. */
	FOnMRNetEvent OnCreateFailed;
	/** A new room's contents arrived (also the first room after entering). */
	FOnMRNetEvent OnRoomEntered;
	FOnMRNetObjectEvent OnObjectAdded;
	FOnMRNetObjectEvent OnObjectChanged;
	FOnMRNetObjectEvent OnObjectMoved;
	FOnMRNetObjectEvent OnObjectRemoved;
	FOnMRNetChat OnChat;
	/** A stat group arrived or a stat in it changed (the group's number). */
	FOnMRNetObjectEvent OnStatsChanged;
	/** The players list changed (BP_PLAYERS, BP_PLAYER_ADD, BP_PLAYER_REMOVE). */
	FOnMRNetEvent OnUsersChanged;
	/** IsWaiting changed. */
	FOnMRNetEvent OnWaitChanged;
	/** A description arrived (BP_LOOK, UC_LOOK_PLAYER): GetDescription. */
	FOnMRNetEvent OnDescription;

private:
	void LoadServers();
	void SetPhase(EMRNetPhase InPhase, const FString& InStatus = FString());
	bool Tick(float DeltaSeconds);

	// game data
	FString CacheDir() const;
	void FetchGameData();
	void OnManifest(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk);
	void OnRsb(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk, FString Hash);
	bool LoadCachedRsb();
	void OpenSocket();

	// messages
	void HandleMessage(const TArray<uint8>& Body);
	void HandleClosed(const FString& Error);
	void AddChat(const FString& Text, uint8 Kind);
	void SetWaiting(bool bInWaiting);

	TArray<FMRServerEntry> Servers;
	int32 LastServer = 0;
	FString LastUser;

	int32 ServerIndex = INDEX_NONE;
	FString PendingUser;
	FString PendingPassword;
	TSharedPtr<FMRConnection> Connection;
	/** Closed connections are released on the next tick, never inside their own callbacks. */
	TArray<TSharedPtr<FMRConnection>> Retired;
	FMRResourceTable Resources;
	FString LoadedRsbHash;

	EMRNetPhase Phase = EMRNetPhase::Offline;
	FString Status;
	FString LastError;
	TArray<FMRCharacterSlot> Characters;
	FString Motd;
	FMRCharInfo CharInfo;
	uint32 CreateSlot = 0;
	bool bSubmittingCharacter = false;
	FMRNetWorld World;
	bool bAwaitingRoom = false;
	bool bWaiting = false;
	int32 RoomsEntered = 0;
	TArray<FMRChatLine> Chat;
	FMRNetDescription Description;
	bool bRequestedStats = false;
	TSharedPtr<FMRAssetCache> Assets;

	FTSTicker::FDelegateHandle TickHandle;
};
