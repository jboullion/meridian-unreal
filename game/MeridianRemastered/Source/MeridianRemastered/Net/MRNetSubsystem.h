#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Net/MRCharInfo.h"
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

/** An overlay on an object (a player's head, face parts, hair, arms, weapon): proto.h's overlay record. */
struct FMRNetOverlay
{
	FString Bgf;           // the bitmap without its extension, lower case ("phax")
	uint8 Hotspot = 0;     // where it attaches (player.kod SendOverlays: 1 head, 11 eyes, 12 mouth, 13 hair, 14 nose...)
	int32 Xlat = -1;       // its palette translation (ANIMATE_TRANSLATION), -1 = none
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
	/** The same overlays with their hotspots and translations. */
	TArray<FMRNetOverlay> OverlayParts;
	/** The object's own palette translation (a player's body: its shirt), -1 = none. */
	int32 Xlat = -1;

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

/**
 * One of the player's statistics as the server describes it (BP_STAT_GROUP / BP_STAT; merintr's
 * Statistic). The server decides which stats exist, their names and order, so the UI lists
 * whatever it sends: Server 104's group 2 is Unbound Energy, Training Pts, the six stats, Karma,
 * Bulk Carried... and the resistances.
 */
struct FMRNetStat
{
	enum EType : uint8 { Numeric = 1, List = 2 };
	uint8 Num = 0;          // the stat's number in its group (not its place: Unbound Energy is 27 but listed first)
	uint32 NameRsc = 0;
	FString Name;
	uint8 Type = Numeric;
	// numeric: the value is an integer (Tag 1, with limits) or a resource (Tag 2, ValueText)
	uint8 Tag = 1;
	int32 Value = 0;
	int32 Min = 0;
	int32 Max = 0;
	int32 CurrentMax = 0;   // what the bar fills to (health: the maximum health; Max is the scale's end)
	FString ValueText;
	// list (spells, skills, quests): the object, its value (an ability percentage) and icon
	uint32 ObjectId = 0;
	uint32 IconRsc = 0;
	FString Icon;           // "ifirebal.bgf"
};

/** A group of stats (BP_STAT_GROUPS names them; Server 104: Condition, Stats, Spells, Skills, Quests). */
struct FMRNetStatGroup
{
	uint8 Group = 0;        // 1-based, as the server numbers them
	uint32 NameRsc = 0;
	FString Name;
	TArray<FMRNetStat> Stats;
	bool bReceived = false;

	const FMRNetStat* FindByNum(uint8 Num) const { return Stats.FindByPredicate([Num](const FMRNetStat& S) { return S.Num == Num; }); }
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
	/** The player's stat groups (empty until the server has sent them, after entering the game). */
	const TArray<FMRNetStatGroup>& GetStatGroups() const { return StatGroups; }
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
	/** The optional translation or effect prefix: the translation, or -1. */
	static int32 ReadPalette(FMRReader& R);
	static void SkipAnimation(FMRReader& R);
	void ReadOverlays(FMRReader& R, TArray<FString>* Out, TArray<FMRNetOverlay>* OutParts = nullptr);
	void AddChat(const FString& Text, uint8 Kind);
	bool ReadStat(FMRReader& R, FMRNetStat& Out);
	FMRNetStatGroup& StatGroup(uint8 Group);

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
	FMRNetPlayer Player;
	bool bAwaitingRoom = false;
	TMap<uint32, FMRNetObject> Objects;
	TArray<FMRChatLine> Chat;
	TArray<FMRNetStatGroup> StatGroups;
	bool bRequestedStats = false;

	FTSTicker::FDelegateHandle TickHandle;
};
