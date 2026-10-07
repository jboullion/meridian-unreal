#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Net/MRResources.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MRNetSubsystem.generated.h"

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
};

/** A character on the account (BP_CHARACTERS). */
struct FMRCharacterSlot
{
	uint32 Id = 0;
	FString Name;
	/** An empty slot: it has to go through character creation first. */
	bool bNeedsCreation = false;
};

/** An object in the player's room, as the server describes it. */
struct FMRNetObject
{
	uint32 Id = 0;         // without the number tag
	uint32 IconRsc = 0;
	uint32 NameRsc = 0;
	FString Icon;          // the bitmap's file name, e.g. "bunny2.bgf"
	FString Name;
	uint32 Flags = 0;      // OF_*
	uint32 MinimapFlags = 0;
	uint8 ObjectType = 0;
	uint8 MoveOn = 0;
	/** Position in Kod fine units: square * 64 + fine, rows and columns starting at 1 (so 64 is the room's edge). */
	int32 KodRow = 0;
	int32 KodCol = 0;
	/** Facing, 0..4095 (0 = east, increasing toward south). */
	int32 Angle = 0;
	/** Last move speed (BP_MOVE), 0 = arrived. */
	uint8 Speed = 0;
	/** Overlay bitmap names (a player's head, arms, weapon...). */
	TArray<FString> Overlays;

	bool IsPlayer() const;
	/** Something alive to draw as a sprite: a player, a monster or an NPC. */
	bool IsCreature() const;
};

/** The player's own character and room (BP_PLAYER). */
struct FMRNetPlayer
{
	uint32 Id = 0;
	uint32 RoomObjectId = 0;
	FString RoomFile;      // "razainn.roo"
	FString RoomName;      // "The Inn of Raza"
	uint32 RoomSecurity = 0;
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
	Characters,   // choosing or creating a character
	Entering,     // waiting for the first room
	InGame,
};

DECLARE_MULTICAST_DELEGATE(FOnMRNetEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetObjectEvent, uint32 /* Id */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetChat, const FMRChatLine&);

/**
 * The session with a Meridian server (docs/adr/0010-meridian-servers.md): the server list, the
 * server's game data, login, character select and creation, and the state of the player's room.
 * UMRNetWorldSubsystem puts that state into the UE world; the login screen and the chat log read it.
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
	void UseCharacter(uint32 Id);
	/** A new character in an empty slot with the server's default face, stats and no spells. */
	void CreateCharacter(uint32 SlotId, const FString& Name, bool bFemale);

	// --- in game
	void RequestMove(int32 KodRow, int32 KodCol, uint8 Speed);
	void RequestTurn(int32 KodAngle);
	void RequestGo();
	void Say(const FString& Text);

	// --- state
	EMRNetPhase GetPhase() const { return Phase; }
	const FString& GetStatus() const { return Status; }
	const FString& GetLastError() const { return LastError; }
	const TArray<FMRCharacterSlot>& GetCharacters() const { return Characters; }
	const FString& GetMotd() const { return Motd; }
	const FMRNetPlayer& GetPlayer() const { return Player; }
	const TMap<uint32, FMRNetObject>& GetObjects() const { return Objects; }
	const FMRNetObject* FindObject(uint32 Id) const { return Objects.Find(Id); }
	const FMRNetObject* GetSelf() const { return Objects.Find(Player.Id); }
	const TArray<FMRChatLine>& GetChat() const { return Chat; }
	const FMRResourceTable& GetResources() const { return Resources; }
	const FMRServerEntry* GetServer() const { return Servers.IsValidIndex(ServerIndex) ? &Servers[ServerIndex] : nullptr; }

	/** Tests and UI shots: show a character list without a server. */
	void SetPreview(EMRNetPhase InPhase, const TArray<FMRCharacterSlot>& InCharacters, const FString& InError = FString());
	/** What the login screen says while it waits (e.g. a room this client hasn't built). */
	void SetStatus(const FString& InStatus) { Status = InStatus; }

	FOnMRNetEvent OnPhaseChanged;
	FOnMRNetEvent OnCharactersChanged;
	/** A new room's contents arrived (also the first room after entering). */
	FOnMRNetEvent OnRoomEntered;
	FOnMRNetObjectEvent OnObjectAdded;
	FOnMRNetObjectEvent OnObjectChanged;
	FOnMRNetObjectEvent OnObjectMoved;
	FOnMRNetObjectEvent OnObjectRemoved;
	FOnMRNetChat OnChat;

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
	bool ReadObject(FMRReader& R, FMRNetObject& Out);
	bool ReadRoomObject(FMRReader& R, FMRNetObject& Out);
	static void SkipPalette(FMRReader& R);
	static void SkipAnimation(FMRReader& R);
	void ReadOverlays(FMRReader& R, TArray<FString>* Out);
	void AddChat(const FString& Text, uint8 Kind);

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
	FMRNetPlayer Player;
	bool bAwaitingRoom = false;
	TMap<uint32, FMRNetObject> Objects;
	TArray<FMRChatLine> Chat;

	FTSTicker::FDelegateHandle TickHandle;
};
