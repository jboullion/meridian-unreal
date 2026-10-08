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
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetProjectile, const FMRNetProjectile&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetHit, const FMRNetHit&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMRNetResult, bool);

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

	// --- items (docs/research/blakserv-protocol.md "Items"; the answers come as BP_INVENTORY_ADD, _REMOVE, BP_USE...)
	/** Ask for the inventory and what is in use again (BP_REQ_INVENTORY). */
	void RequestInventory();
	/** Wear, wield or use a carried item (BP_REQ_USE); take it off (BP_REQ_UNUSE). */
	void UseItem(uint32 ItemId);
	void UnuseItem(uint32 ItemId);
	/** Pick something up from the room (BP_REQ_GET). */
	void Pickup(uint32 ObjectId);
	/** Take from the container last looked into (BP_REQ_GET_FROM_CONTAINER); Amount for a number item (0 = all). */
	void PickupFromContainer(uint32 ItemId, uint32 Amount = 0);
	/** Drop a carried item (BP_REQ_DROP); Amount for a number item (0 = all). */
	void Drop(uint32 ItemId, uint32 Amount = 0);
	/** Put a carried item into a container (BP_REQ_PUT). */
	void Put(uint32 ItemId, uint32 ContainerId, uint32 Amount = 0);
	/** Use an item on something (BP_REQ_APPLY: a key on a door, a scroll on a player). */
	void Apply(uint32 ItemId, uint32 TargetId);
	/** Work something in the room (BP_REQ_ACTIVATE: a lever, a fountain). */
	void Activate(uint32 ObjectId);
	/** Look into a container (BP_SEND_OBJECT_CONTENTS); the answer fires OnContents. */
	void RequestContents(uint32 ContainerId);
	/** Move a carried item to another's place in the list (BP_REQ_INVENTORY_MOVE; not items in use). */
	void MoveInventoryItem(uint32 ItemId, uint32 PlaceOfId);
	const TArray<FMRNetObject>& GetInventory() const { return World.Inventory; }

	// --- combat (docs/research/blakserv-protocol.md "Combat")
	/**
	 * Attack something (BP_REQ_ATTACK, ATTACK_NORMAL). The server decides everything: range, the
	 * 1 s between attacks, the hit; it answers with messages (OnHit), the swing (BP_CHANGE on us,
	 * BP_PLAYER_OVERLAY in first person) and sounds.
	 */
	void Attack(uint32 TargetId);
	// --- spells, skills, enchantments (docs/research/blakserv-protocol.md "Spells and skills")
	/** Cast a spell (BP_REQ_CAST): its id and its targets (none for a spell that takes none). */
	void CastSpell(uint32 SpellId, const TArray<uint32>& Targets);
	/** Sit down to rest, or stand up again (BP_USERCOMMAND UC_REST, UC_STAND). */
	void Rest();
	void Stand();
	/** Answer a retraining offer (BP_CHANGED_STATS: the six stats; the school levels as offered). */
	void ChangeStats(const uint8 (&Stats)[6]);
	const TArray<FMRNetSpell>& GetSpells() const { return World.Spells; }
	const TArray<FMRNetObject>& GetSkills() const { return World.Skills; }
	const TArray<FMRNetObject>& GetPlayerEnchantments() const { return World.PlayerEnchantments; }
	const TArray<FMRNetObject>& GetRoomEnchantments() const { return World.RoomEnchantments; }
	const FMRNetStatChange& GetStatChange() const { return World.StatChange; }

	// --- shops, offers, the vault and the bank (docs/research/blakserv-protocol.md "Trade")
	/** An item and how many (number items: shillings, reagents; 0 = all of a carried stack). */
	struct FItemCount
	{
		uint32 Id = 0;
		uint32 Amount = 0;
	};
	/** Ask a seller what it sells (BP_REQ_BUY); the list comes as OnShop. */
	void RequestBuy(uint32 SellerId);
	/** Buy from the last list (BP_REQ_BUY_ITEMS), or take out of the vault (BP_REQ_WITHDRAWAL_ITEMS). */
	void BuyItems(const TArray<FItemCount>& Items);
	/** Ask a vault keeper what we have there (BP_REQ_WITHDRAWAL). */
	void RequestWithdrawal(uint32 KeeperId);
	/** Offer carried items to someone (BP_REQ_OFFER): selling to an NPC, or a trade with a player. */
	void Offer(uint32 ToId, const TArray<FItemCount>& Items);
	/** Put carried items into a vault (BP_REQ_DEPOSIT; a banker takes shillings this way too). */
	void Deposit(uint32 ToId, const TArray<FItemCount>& Items);
	/** Answer an offer made to us with what we give (BP_REQ_COUNTEROFFER; nothing is fine). */
	void Counteroffer(const TArray<FItemCount>& Items);
	/** Accept their answer to our offer (BP_ACCEPT_OFFER), or call the offer off (BP_CANCEL_OFFER). */
	void AcceptOffer();
	void CancelOffer();
	/** A banker in the room: put in or take out shillings, ask the balance (UC_DEPOSIT, UC_WITHDRAW, UC_BALANCE). */
	void BankDeposit(int32 Shillings);
	void BankWithdraw(int32 Shillings);
	void BankBalance();
	const FMRNetShop& GetShop() const { return World.Shop; }
	const FMRNetTrade& GetTrade() const { return World.Trade; }

	/** A line of the client's own in the chat log, as the original's GameMessage (e.g. "You can't see your selected target."). */
	void AddGameMessage(const FString& Text) { AddChat(Text, 0); }
	/** The screen effects on the player and the room's weather (BP_EFFECT), counted down each tick. */
	const FMRNetEffects& GetEffects() const { return World.Effects; }
	/**
	 * Tests and the MREffect console command: handle a BP_EFFECT as if the server had sent it
	 * (Ms: the duration where the effect has one; Xlat: the flash's or the override's translation).
	 */
	void DebugEffect(uint16 Effect, int32 Ms, int32 Xlat);
	const FMRNetObject* FindInventory(uint32 Id) const { return World.FindInventory(Id); }
	bool IsUsing(uint32 Id) const { return World.Using.Contains(Id); }

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
	/** The inventory or what is in use changed. */
	FOnMRNetEvent OnInventoryChanged;
	/** A container's contents arrived (BP_OBJECT_CONTENTS): GetNetWorld().ContentsOf, Contents. */
	FOnMRNetEvent OnContents;
	/** A BP_EFFECT changed GetEffects() (the weather too). */
	FOnMRNetEvent OnEffect;
	/** Something was shot across the room (BP_SHOOT, BP_RADIUS_SHOOT). */
	FOnMRNetProjectile OnProjectile;
	/** A combat message with damage in it: we hit something, or were hit. */
	FOnMRNetHit OnHit;
	/** The spells or skills changed (BP_SPELLS, BP_SKILLS, their ADD / REMOVE). */
	FOnMRNetEvent OnAbilitiesChanged;
	/** An enchantment came or went, on the player or the room. */
	FOnMRNetEvent OnEnchantmentsChanged;
	/** A retraining offer arrived (BP_STAT_CHANGE): GetStatChange. */
	FOnMRNetEvent OnStatChange;
	/** The server's answer to ChangeStats: true BP_CHANGED_STATS_OK, false NOT_OK. */
	FOnMRNetResult OnStatChangeResult;
	/** A shop's or a vault's list arrived (GetShop). */
	FOnMRNetEvent OnShop;
	/** The offer under way changed, or ended (GetTrade().bOpen false). */
	FOnMRNetEvent OnTradeChanged;

private:
	void LoadServers();
	/** Send an item: its id, and for a number item the tagged id and an amount (clientd3d protocol.c PARAM_OBJECT). */
	void WriteItem(class FMRWriter& W, uint32 ItemId, uint32 Amount) const;
	/** PARAM_OBJECT_LIST (clientd3d protocol.c): u16 count, then each item (number items with their amount). */
	void WriteItems(class FMRWriter& W, const TArray<FItemCount>& Items) const;
	bool CanSend() const;
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
	/** The hit messages' format ids in the loaded rsb, by MRNetRead::Hit's kind (found by their Kod text). */
	const TMap<uint32, int32>& HitFormats();

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
	TMap<uint32, int32> HitFormatIds;
	FString HitFormatsRsb;

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
