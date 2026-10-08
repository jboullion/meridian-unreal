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
	MERIDIANREMASTERED_API bool Animation(FMRReader& R, FMRNetAnimation& Out);
	/** The optional translation-or-effect prefix (ANIMATE_TRANSLATION / ANIMATE_EFFECT). */
	MERIDIANREMASTERED_API void Palette(FMRReader& R, int32& OutXlat, int32& OutEffect);
	MERIDIANREMASTERED_API bool Overlays(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetOverlay>& Out);
	/** An object (clientd3d ExtractObject): id (and amount), names, flags, light, translation, animation, overlays. */
	MERIDIANREMASTERED_API bool Object(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** The motion record (what an object shows while it moves): translation prefix, animation, overlays. */
	MERIDIANREMASTERED_API bool Motion(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** An object in a room (ExtractNewRoomObject): the object, its position and angle, its motion record. */
	MERIDIANREMASTERED_API bool RoomObject(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** An object without its light (ExtractObjectNoLight: BP_PLAYER_OVERLAY's). */
	MERIDIANREMASTERED_API bool ObjectNoLight(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out);
	/** A list of objects (ExtractObjectList: u16 count, then each object): BP_INVENTORY, BP_OBJECT_CONTENTS. */
	MERIDIANREMASTERED_API bool ObjectList(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetObject>& Out);
	/** A list of ids (u16 count, then u32 each): BP_USE_LIST. */
	MERIDIANREMASTERED_API bool IdList(FMRReader& R, TArray<uint32>& Out);
	/** BP_LOOK's body after the type byte. */
	MERIDIANREMASTERED_API bool Look(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out);
	/** UC_LOOK_PLAYER's body after BP_USERCOMMAND's type bytes. */
	MERIDIANREMASTERED_API bool LookPlayer(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out);
	/** BP_PLAYER's body after the type byte. */
	MERIDIANREMASTERED_API bool Player(FMRReader& R, const FMRResourceTable& Res, FMRNetPlayer& Out);
	/** One entry of BP_PLAYERS, or BP_PLAYER_ADD's body (the name comes as a string, not a resource). */
	MERIDIANREMASTERED_API bool User(FMRReader& R, FMRNetUser& Out);
	/** A stat (merintr.c ExtractStatistic). */
	MERIDIANREMASTERED_API bool Stat(FMRReader& R, const FMRResourceTable& Res, FMRNetStat& Out);
	/** A .roo file's security value (the u32 after its magic and version; clientd3d bspload.c), or 0. */
	MERIDIANREMASTERED_API uint32 RooSecurity(const TArray<uint8>& RooBytes);
	/** Whether a room's security matches the server's: the low 28 bits only (clientd3d game.c). */
	inline bool SecurityMatches(uint32 A, uint32 B) { return (A & 0x0FFFFFFF) == (B & 0x0FFFFFFF); }
}
