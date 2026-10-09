#pragma once

#include "CoreMinimal.h"

class FMRReader;
class FMRResourceTable;

/**
 * The client's model of what a Meridian server has told it (docs/adr/0012-client-parity-and-world-coverage.md):
 * the player and their room, the objects in it, the players logged on, and the stat groups. Plain
 * data and readers, no UE objects: UMRNetSubsystem fills it from messages, the world and the UI read
 * it, and the automation tests parse messages into it without a server.
 *
 * Layouts are blakserv's (written from clientd3d server.c ExtractObject and friends, never ported).
 */

/** An animation record (proto.h ANIMATE_*): which bitmap group an object or overlay shows. */
struct FMRNetAnimation
{
	uint8 Type = 0;          // ANIMATE_NONE (a fixed group), ANIMATE_CYCLE, ANIMATE_ONCE
	uint16 Group = 0;        // ANIMATE_NONE
	uint32 Period = 0;       // ms per group, CYCLE and ONCE
	uint16 GroupLow = 0;
	uint16 GroupHigh = 0;
	uint16 GroupFinal = 0;   // ONCE: where it stays afterwards
};

/** An overlay on an object (a player's head, face parts, hair, arms, weapon): proto.h's overlay record. */
struct FMRNetOverlay
{
	FString Bgf;           // the bitmap without its extension, lower case ("phax")
	uint8 Hotspot = 0;     // where it attaches (player.kod SendOverlays: 1 head, 11 eyes, 12 mouth, 13 hair, 14 nose...)
	int32 Xlat = -1;       // its palette translation (ANIMATE_TRANSLATION), -1 = none
	FMRNetAnimation Animation;
};

/** A light an object gives off (clientd3d server.c ExtractDLighting). */
struct FMRNetLight
{
	uint16 Flags = 0;      // LIGHT_FLAG_*; 0 = no light
	uint8 Intensity = 0;
	uint16 Color = 0;      // 5:5:5 RGB
};

/** An object as the server describes it: in the room, in a list (inventory, look) or a container. */
struct FMRNetObject
{
	uint32 Id = 0;         // without the number tag
	/** How many, for a number item (shillings, arrows); 0 for anything else. */
	uint32 Amount = 0;
	/** A number item (its id came with CLIENT_TAG_NUMBER): sent back with an amount. */
	bool bNumber = false;
	uint32 IconRsc = 0;
	uint32 NameRsc = 0;
	FString Icon;          // the bitmap's file name, e.g. "bunny2.bgf"
	FString Name;
	uint32 Flags = 0;      // OF_*
	uint8 DrawEffect = 0;  // DRAWFX_* (translucent, black, invisible...)
	uint32 MinimapFlags = 0;
	uint32 NameColor = 0;  // 0x00RRGGBB
	uint8 ObjectType = 0;
	uint8 MoveOn = 0;
	FMRNetLight Light;
	/** The object's own palette translation (a player's body: its shirt), -1 = none. */
	int32 Xlat = -1;
	/** A drawing effect given as the translation prefix (ANIMATE_EFFECT), -1 = none. */
	int32 Effect = -1;
	FMRNetAnimation Animation;
	/** Its overlays (a player's head, face parts, arms, weapon...) with their hotspots and translations. */
	TArray<FMRNetOverlay> OverlayParts;

	// --- in a room only (BP_ROOM_CONTENTS, BP_CREATE)
	/** Position in Kod fine units: square * 64 + fine, rows and columns starting at 1 (so 64 is the room's edge). */
	int32 KodRow = 0;
	int32 KodCol = 0;
	/** Facing, 0..4095 (0 = east, increasing toward south). */
	int32 Angle = 0;
	/** Last move speed (BP_MOVE), 0 = arrived. */
	uint8 Speed = 0;
	/** What it shows while moving: its translation, animation and overlays (the "motion" record). */
	int32 MotionXlat = -1;
	FMRNetAnimation MotionAnimation;
	TArray<FMRNetOverlay> MotionOverlays;

	bool IsPlayer() const;
	/** Something alive to draw as a sprite: a player, a monster or an NPC. */
	bool IsCreature() const;
};

/**
 * A first-person picture (BP_PLAYER_OVERLAY; clientd3d overlay.c SetPlayerOverlay): its slot is
 * the object's id (blakston.khd PWO_LEFT_HAND 1, PWO_RIGHT_HAND 2), where on the screen it goes
 * (HS_NW 1 .. HS_CENTER 9, 0 = hidden), the bitmap and its animation (group 0 = nothing drawn).
 */
struct FMRNetPlayerOverlay
{
	uint8 Hotspot = 0;
	FMRNetObject Object;
	uint32 Seq = 0;           // FMRNetWorld::PlayerOverlaySeq when it came
};

/** The player's own character and room (BP_PLAYER; clientd3d server.c HandlePlayer). */
struct FMRNetPlayer
{
	uint32 Id = 0;
	uint32 IconRsc = 0;
	uint32 NameRsc = 0;
	uint32 RoomObjectId = 0;
	FString RoomFile;      // "razainn.roo"
	FString RoomName;      // "The Inn of Raza"
	/** The room's security value; its low 28 bits must equal the .roo header's (MRNetRead::RooSecurity). */
	uint32 RoomSecurity = 0;
	uint8 AmbientLight = 0;
	uint8 PlayerLight = 0;
	/** The sky background's bitmap (BP_BACKGROUND changes it later). */
	uint32 BackgroundRsc = 0;
	FString Background;
	uint32 WadingSoundRsc = 0;
	FString WadingSound;
	uint32 RoomFlags = 0;  // ROOM_FLAG_* (blakston.khd)
	/** The server's override of the three wading depths (SF_DEPTH1..3), in its units; 0 = the room's own. */
	uint32 Depth[3] = {};
	/** The sun on the room (BP_LIGHT_SHADING; user.kod ToCliShading): its strength, angle and height. */
	uint8 DirectionalLight = 0;
	uint16 SunAngle = 0;
	uint16 SunHeight = 0;
};

/** A player logged on to the server (BP_PLAYERS, BP_PLAYER_ADD; the "who" list). */
struct FMRNetUser
{
	uint32 Id = 0;
	uint32 NameRsc = 0;
	FString Name;
	uint32 Flags = 0;
	uint8 DrawEffect = 0;
	uint32 MinimapFlags = 0;
	uint32 NameColor = 0;
	uint8 ObjectType = 0;
	uint8 MoveOn = 0;
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

/**
 * A description to show: BP_LOOK for an object (clientd3d server.c HandleLook) or BP_USERCOMMAND
 * UC_LOOK_PLAYER for a player (merintr.c HandleLookPlayer).
 */
struct FMRNetDescription
{
	FMRNetObject Object;
	bool bPlayer = false;
	/** BP_LOOK's DF_* flags: DF_INSCRIBED (a sign, a scroll) has an inscription; DF_EDITABLE can be written. */
	uint8 Flags = 0;
	/** A player's own description (or an admin's view): it can be changed (BP_CHANGE_DESCRIPTION). */
	bool bEditable = false;
	/** The description, formatted (style codes kept: MRServerText::StripStyle removes them). */
	FString Text;
	FString Inscription;
	/** A player's extra lines (guild, title...) and web page. */
	FString ExtraInfo;
	FString Url;
};

/** Something a shop sells (BP_BUY_LIST) or a vault holds (BP_WITHDRAWAL_LIST): the item and its price or fee. */
struct FMRNetForSale
{
	FMRNetObject Object;
	uint32 Price = 0;
};

/** A shop's or a vault's list (clientd3d server.c HandleBuyList, HandleWithdrawalList). */
struct FMRNetShop
{
	/** The seller (a shopkeeper, a vault keeper). */
	FMRNetObject Seller;
	TArray<FMRNetForSale> Items;
	/** A vault's list: taking an item out costs its fee (BP_REQ_WITHDRAWAL_ITEMS), else buying it (BP_REQ_BUY_ITEMS). */
	bool bWithdrawal = false;
};

/**
 * An offer under way (clientd3d offer.c; Kod user.kod UserOffer, Offer, UserCounterOffer):
 * - we offer something to someone (BP_REQ_OFFER): the server shows us what we offered (BP_OFFERED);
 *   they answer with what they give for it (BP_COUNTEROFFER: an NPC's price in shillings); we accept
 *   (BP_ACCEPT_OFFER) or cancel;
 * - someone offers to us (BP_OFFER: who, and what): we answer with what we give (BP_REQ_COUNTEROFFER;
 *   BP_COUNTEROFFERED shows it back), and they accept or cancel.
 * BP_OFFER_CANCELED ends it either way (also after the other side accepted).
 */
struct FMRNetTrade
{
	bool bOpen = false;
	/** We made the offer (else it was made to us). */
	bool bOurs = false;
	/** The other side: its object, name. */
	uint32 WithId = 0;
	FString WithName;
	/** What we give and what they give, as the server listed them. */
	TArray<FMRNetObject> Given;
	TArray<FMRNetObject> Received;
	/** They answered (BP_COUNTEROFFER): ours to accept. */
	bool bAnswered = false;
	/** We answered (BP_COUNTEROFFERED): theirs to accept. */
	bool bCountered = false;
};

/**
 * A sound the server plays or stops (BP_PLAY_WAVE, BP_STOP_WAVE, BP_PLAY_MUSIC / _MIDI; clientd3d
 * server.c HandlePlayWave, game.c GamePlaySound, audio.c): at an object, at a square, or 2D.
 */
struct FMRNetSound
{
	enum class EKind : uint8 { Play, Stop, Music, StopLoops };
	EKind Kind = EKind::Play;
	FString File;
	/** Where it comes from: an object in the room (0: none). */
	uint32 ObjectId = 0;
	/** SF_LOOP, SF_RANDOM_PLACE... */
	uint8 Flags = 0;
	/** A square (1-based; 0, 0: 2D unless ObjectId). */
	int32 Row = 0;
	int32 Col = 0;
};

/**
 * A change the server makes to the room (clientd3d server.c HandleSectorMove, HandleSectorChange,
 * HandleChangeTexture; roomanim.c). On entering a room the server sends all of its changes again
 * (room.kod SendSectorChanges...), lifts at speed 0.
 */
struct FMRNetRoomChange
{
	enum class EKind : uint8 { MoveSector, ChangeSector, ChangeTexture };
	EKind Kind = EKind::MoveSector;
	/** The sectors' (and for a texture, the sidedefs') server id. */
	uint16 Id = 0;
	/** MoveSector: ANIMATE_FLOOR_LIFT or ANIMATE_CEILING_LIFT; the height (Kod units, as the .roo's) and speed (a second; 0: at once). */
	uint8 Type = 0;
	int16 Height = 0;
	uint8 Speed = 0;
	/** ChangeSector: the depth (0..3) and scroll speed (0..3); CHANGE_OVERRIDE keeps one. */
	uint8 Depth = 0;
	uint8 Scroll = 0;
	/** ChangeTexture: the texture (grdNNNNN.bgf) and which surfaces (CTF_*). */
	uint16 Texture = 0;
	uint8 Flags = 0;
};

/**
 * A mail message (BP_MAIL; mailnews.c HandleMail, user.kod UserGetNewMail): the server's index
 * (BP_DELETE_MAIL once we've kept it), the sender, the time, the recipients, and the text, whose
 * first line is "Subject: ..." when it has one.
 */
struct FMRNetMail
{
	uint32 Index = 0;
	FString From;
	/** Unix seconds. */
	int64 Time = 0;
	TArray<FString> To;
	FString Subject;
	FString Body;
};

/** A news board's article heading (BP_ARTICLES). */
struct FMRNetArticle
{
	uint32 Num = 0;
	int64 Time = 0;  // Unix seconds
	FString Poster;
	FString Title;
};

/** The news board looked at (BP_LOOK_NEWSGROUP) and what we've read of it (BP_ARTICLES, BP_ARTICLE). */
struct FMRNetNews
{
	bool bOpen = false;
	uint16 Group = 0;
	uint8 Permission = 0;  // NEWS_READ, NEWS_POST
	FMRNetObject Board;
	FString Description;
	TArray<FMRNetArticle> Articles;
	/** The headings came in full (BP_ARTICLES comes in parts). */
	bool bHaveArticles = false;
	uint32 ReadingNum = 0;
	FString ReadingText;
	bool bHaveText = false;
};

struct FMRNetGuildMember
{
	uint32 Id = 0;
	FString Name;
	uint8 Rank = 0;    // 1 (lowest) .. 5 (the guildmaster)
	uint8 Gender = 0;  // 1 male, 2 female (the rank's name)
};

/** The player's guild (UC_GUILDINFO; merintr.c HandleGuildInfo, user.kod UserGuildSendInfo). */
struct FMRNetGuild
{
	bool bValid = false;
	FString Name;
	bool bHasPassword = false;
	FString Password;
	uint32 Flags = 0;  // GC_*: what we may do
	uint32 GuildId = 0;
	FString MaleRanks[5];    // MRMsg::GuildRanks
	FString FemaleRanks[5];
	uint32 CurrentVote = 0;
	TArray<FMRNetGuildMember> Members;
};

/** Every guild and our guild's ties (UC_GUILD_LIST; user.kod UserGuildSendList). */
struct FMRNetGuildList
{
	bool bValid = false;
	TArray<TPair<uint32, FString>> Guilds;
	TArray<uint32> Allies, Enemies, DeclaredAllies, DeclaredEnemies;
};

/** A spell the character knows (BP_SPELLS, BP_SPELL_ADD; merintr.c ExtractNewSpell). */
struct FMRNetSpell
{
	/** Its id (what BP_REQ_CAST names), name and icon. */
	FMRNetObject Object;
	/** How many targets it takes: 0 casts straight away (merintr spells.c SpellCast). */
	uint8 Targets = 0;
	/** Its school as the server numbers it (1 Shal'ille .. 6 Jala; merintr subtracts 1). */
	uint8 School = 0;
};

/**
 * Retraining (BP_STAT_CHANGE; user.kod SendStatChange, a town elder's offer): the six stats (might,
 * intellect, stamina, agility, mysticism, aim) and the school levels shown beside them (Shal'ille,
 * Qor, Kraanan, Faren, Riija, Jala, weaponcraft, crafting). The answer is BP_CHANGED_STATS with the
 * same 14 bytes, the stats changed.
 */
struct FMRNetStatChange
{
	uint8 Stats[6] = {};
	uint8 Levels[8] = {};
};

/**
 * The screen effects the server has put on the player (BP_EFFECT; clientd3d effect.c PerformEffect).
 * Times are the milliseconds left, counted down by Tick as the original's AnimateEffects did.
 */
struct FMRNetEffects
{
	float PainMs = 0.f;      // red over the view, fading (draw3d.c: 80% above 2 s, down to 10%)
	float WhiteoutMs = 0.f;  // white, fading over its last half second
	float InvertMs = 0.f;    // the view's colours inverted
	float ShakeMs = 0.f;     // the eye jiggles (effect.c EffectShake)
	float BlurMs = 0.f;      // added up, at most 200 s (drunk)
	float WaverMs = 0.f;     // the view sways sideways (vertigo)
	/** A colour flash (an XLAT_BLEND* translation) and how long it has left. */
	uint32 FlashXlat = 0;
	float FlashMs = 0.f;
	/** A translation over the whole view until it is turned off (0 = none). */
	uint32 XlatOverride = 0;
	bool bBlind = false;     // nothing of the world is drawn
	bool bParalyzed = false; // no walking or turning
	/** The room's weather: EFFECT_RAINING, EFFECT_SNOWING or EFFECT_FIREWORKS; 0 = clear. Sent on every room change. */
	uint16 Weather = 0;
	bool bSand = false;
	/** Bumped by every BP_EFFECT. */
	uint32 Seq = 0;

	/** Apply one BP_EFFECT (the u16 effect and its parameters); false if unknown or cut short. */
	bool Apply(FMRReader& R);
	void Tick(float Ms);
	void Reset() { *this = FMRNetEffects(); }
};

/**
 * Something thrown or cast across the room (BP_SHOOT: from one object to another; BP_RADIUS_SHOOT:
 * Number of them out from an object to Range squares, evenly round). clientd3d server.c HandleShoot,
 * project.c: it flies at Speed squares a second and is gone on arrival.
 */
struct FMRNetProjectile
{
	uint32 IconRsc = 0;
	FString Icon;
	int32 Xlat = -1;
	int32 Effect = -1;
	FMRNetAnimation Animation;
	uint32 Source = 0;
	uint32 Dest = 0;
	uint8 Speed = 0;       // squares a second; 0 = arrives at once
	uint16 Flags = 0;      // PROJ_FLAG_FOLLOWGROUND: it keeps to the floor
	uint8 Range = 0;
	uint8 Number = 0;
	FMRNetLight Light;
	bool bRadius = false;
};

/**
 * Damage in a combat message: battler.kod AssessHit tells the attacker "Your mace wounds the rat
 * for 4 damage." (battler_attacker_hit, _mob) and the one hit "The rat's bite wounds you for 2
 * damage." (battler_defender_hit, _mob). Damage numbers are ours; the original only printed the line.
 */
struct FMRNetHit
{
	/** We hit something (else something hit us). */
	bool bDealt = true;
	/** The other one's name, without its article ("rat", or a player's name). */
	FString Name;
	int32 Damage = 0;
};

/** Everything the server has said about the session's game state. */
struct FMRNetWorld
{
	FMRNetPlayer Player;
	TMap<uint32, FMRNetObject> Objects;
	/** What the player holds as the original drew it in first person (BP_PLAYER_OVERLAY), by slot (PWO_*). */
	TMap<uint32, FMRNetPlayerOverlay> PlayerOverlays;
	/** Counts BP_PLAYER_OVERLAYs, so a drawer sees each one (the same swing again restarts it). */
	uint32 PlayerOverlaySeq = 0;
	/** Who is logged on, by object id. */
	TMap<uint32, FMRNetUser> Users;
	/**
	 * What the player carries (BP_INVENTORY, _ADD, _REMOVE), in the server's order: the items in
	 * use first, then the rest (user.kod ToCliInventory). Kept across rooms.
	 */
	TArray<FMRNetObject> Inventory;
	bool bHasInventory = false;
	/** The carried items in use: worn, wielded (BP_USE_LIST, BP_USE, BP_UNUSE). */
	TSet<uint32> Using;
	/** The last container looked into (BP_OBJECT_CONTENTS): its id and what it holds. */
	uint32 ContentsOf = 0;
	TArray<FMRNetObject> Contents;
	/** The screen effects on the player and the room's weather (BP_EFFECT). Kept across rooms, as the original. */
	FMRNetEffects Effects;
	/** The spells and skills the character knows (BP_SPELLS, BP_SKILLS and their ADD / REMOVE), in the server's order. */
	TArray<FMRNetSpell> Spells;
	bool bHasSpells = false;
	TArray<FMRNetObject> Skills;
	bool bHasSkills = false;
	/** Enchantments on the player (kept across rooms) and on the room (merintr enchant.c: reset on each new room). */
	TArray<FMRNetObject> PlayerEnchantments;
	TArray<FMRNetObject> RoomEnchantments;
	/** The last retraining offer (BP_STAT_CHANGE). */
	FMRNetStatChange StatChange;
	/** The last shop or vault list (BP_BUY_LIST, BP_WITHDRAWAL_LIST), and the offer under way. */
	FMRNetShop Shop;
	FMRNetTrade Trade;
	/** The news board being read, the player's guild, every guild, and the server-kept options. */
	FMRNetNews News;
	FMRNetGuild Guild;
	FMRNetGuildList GuildList;
	/** The server asked us to found a guild (UC_GUILD_ASK, a guild creator): its two prices. */
	int32 GuildCost = 0;
	int32 GuildSecretCost = 0;
	uint32 Preferences = 0;
	bool bHasPreferences = false;
	/** The room's changes since BP_PLAYER, in order: a room still being built takes them all when it's ready. */
	TArray<FMRNetRoomChange> RoomChanges;

	const FMRNetSpell* FindSpell(uint32 Id) const { return Spells.FindByPredicate([Id](const FMRNetSpell& S) { return S.Object.Id == Id; }); }
	/** Forget the spells, skills and the player's enchantments (logged off, or asked for again after a save). */
	void ResetAbilities();

	const FMRNetObject* FindInventory(uint32 Id) const { return Inventory.FindByPredicate([Id](const FMRNetObject& O) { return O.Id == Id; }); }
	/** Forget the inventory (logged off, or the server renumbered its objects). */
	void ResetInventory();
	TArray<FMRNetStatGroup> StatGroups;

	/** Forget the room and its objects and who is on (the server sends them again on request). */
	void ResetRoom();
	/** Forget everything (logged off). */
	void Reset();
	FMRNetStatGroup& StatGroup(uint8 Group);
	const FMRNetStatGroup* FindStatGroup(uint8 Group) const;
};

/** Readers for the server's records. Each returns false when the message is cut short. */
namespace MRNetRead
{
	UNREALMERIDIAN_API bool Animation(FMRReader& R, FMRNetAnimation& Out);
	/** The optional translation-or-effect prefix (ANIMATE_TRANSLATION / ANIMATE_EFFECT). */
	UNREALMERIDIAN_API void Palette(FMRReader& R, int32& OutXlat, int32& OutEffect);
	UNREALMERIDIAN_API bool Overlays(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetOverlay>& Out);
	/** An object (clientd3d ExtractObject): id (and amount), names, flags, light, translation, animation, overlays. */
	UNREALMERIDIAN_API bool Object(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** The motion record (what an object shows while it moves): translation prefix, animation, overlays. */
	UNREALMERIDIAN_API bool Motion(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** An object in a room (ExtractNewRoomObject): the object, its position and angle, its motion record. */
	UNREALMERIDIAN_API bool RoomObject(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** An object without its light (ExtractObjectNoLight: BP_PLAYER_OVERLAY's). */
	UNREALMERIDIAN_API bool ObjectNoLight(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** A list of objects (ExtractObjectList: u16 count, then each object): BP_INVENTORY, BP_OBJECT_CONTENTS. */
	UNREALMERIDIAN_API bool ObjectList(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetObject>& Out);
	/** A list of ids (u16 count, then u32 each): BP_USE_LIST. */
	UNREALMERIDIAN_API bool IdList(FMRReader& R, TArray<uint32>& Out);
	/** BP_LOOK's body after the type byte. */
	UNREALMERIDIAN_API bool Look(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out);
	/** UC_LOOK_PLAYER's body after BP_USERCOMMAND's type bytes. */
	UNREALMERIDIAN_API bool LookPlayer(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out);
	/** BP_PLAYER's body after the type byte. */
	UNREALMERIDIAN_API bool Player(FMRReader& R, const FMRResourceTable& Res, FMRNetPlayer& Out);
	/** BP_MAIL: one message, or (no recipients) the end of the new mail (bEnd). */
	UNREALMERIDIAN_API bool Mail(FMRReader& R, const FMRResourceTable& Res, FMRNetMail& Out, bool& bEnd);
	/** BP_ARTICLES: one part of a board's headings, appended to Out (Part and Parts say which). */
	UNREALMERIDIAN_API bool Articles(FMRReader& R, uint16& Group, uint8& Part, uint8& Parts, TArray<FMRNetArticle>& Out);
	/** BP_LOOK_NEWSGROUP: the board, what we may do there and its description. */
	UNREALMERIDIAN_API bool LookNewsgroup(FMRReader& R, const FMRResourceTable& Res, FMRNetNews& Out);
	/** BP_LOOKUP_NAMES: the players' ids for the names asked about, in order; 0 for an unknown name. */
	UNREALMERIDIAN_API bool LookupNames(FMRReader& R, TArray<uint32>& Out);
	/** UC_GUILDINFO and UC_GUILD_LIST (after the user command's byte). */
	UNREALMERIDIAN_API bool GuildInfo(FMRReader& R, FMRNetGuild& Out);
	UNREALMERIDIAN_API bool GuildList(FMRReader& R, FMRNetGuildList& Out);
	/** "Subject: x\nbody" (or "Betreff: ") into its subject and the rest. */
	UNREALMERIDIAN_API void SplitSubject(const FString& Text, FString& OutSubject, FString& OutBody);
	/** BP_SECTOR_MOVE, BP_SECTOR_CHANGE or BP_CHANGE_TEXTURE's body (Type: which). */
	UNREALMERIDIAN_API bool RoomChange(uint8 Type, FMRReader& R, FMRNetRoomChange& Out);
	/** One entry of BP_PLAYERS, or BP_PLAYER_ADD's body (the name comes as a string, not a resource). */
	UNREALMERIDIAN_API bool User(FMRReader& R, FMRNetUser& Out);
	/** A stat (merintr.c ExtractStatistic). */
	UNREALMERIDIAN_API bool Stat(FMRReader& R, const FMRResourceTable& Res, FMRNetStat& Out);
	/** A spell (ExtractNewSpell): the object, then u8 targets and u8 school. */
	UNREALMERIDIAN_API bool Spell(FMRReader& R, const FMRResourceTable& Res, FMRNetSpell& Out);
	/** BP_SPELLS: u16 count, then spells. */
	UNREALMERIDIAN_API bool SpellList(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetSpell>& Out);
	/** BP_BUY_LIST / BP_WITHDRAWAL_LIST: the seller, u16 count, then each item and its u32 price. */
	UNREALMERIDIAN_API bool BuyList(FMRReader& R, const FMRResourceTable& Res, FMRNetShop& Out);
	/** BP_STAT_CHANGE's 14 bytes. */
	UNREALMERIDIAN_API bool StatChange(FMRReader& R, FMRNetStatChange& Out);
	/** BP_SHOOT's body (bRadius false) or BP_RADIUS_SHOOT's, after the type byte. */
	UNREALMERIDIAN_API bool Projectile(FMRReader& R, const FMRResourceTable& Res, bool bRadius, FMRNetProjectile& Out);
	/**
	 * The damage in a BP_MESSAGE, when its format is one of the hit messages (Kind: 1 we hit a player,
	 * 2 a monster, 3 a player hit us, 4 a monster did). R is at the parameters, after the format id.
	 */
	UNREALMERIDIAN_API bool Hit(FMRReader R, const FMRResourceTable& Res, int32 Kind, FMRNetHit& Out);
	/** A .roo file's security value (the u32 after its magic and version; clientd3d bspload.c), or 0. */
	UNREALMERIDIAN_API uint32 RooSecurity(const TArray<uint8>& RooBytes);
	/** Whether a room's security matches the server's: the low 28 bits only (clientd3d game.c). */
	inline bool SecurityMatches(uint32 A, uint32 B) { return (A & 0x0FFFFFFF) == (B & 0x0FFFFFFF); }
}
