#pragma once

#include "CoreMinimal.h"

/**
 * The Meridian 59 wire protocol (docs/research/blakserv-protocol.md), written from reading
 * ReferenceServers/Server-104 (blakserv, include/proto.h). Transport-agnostic: bytes in, bytes out.
 *
 * - Everything is little-endian. A string is a u16 length and that many Latin-1 bytes (no NUL).
 * - Frames: u16 length, u16 CRC (login mode) or security word (game mode), u16 length again,
 *   u8 epoch, then the body. The body's first byte is the message type.
 */
namespace MRMsg
{
	// login mode (AP_*), client -> server
	constexpr uint8 AP_LOGIN = 2;
	constexpr uint8 AP_REQ_GAME = 4;
	// login mode, server -> client
	constexpr uint8 AP_GETCLIENT = 7;
	constexpr uint8 AP_CLIENT_PATCH = 13;
	constexpr uint8 AP_GETLOGIN = 21;
	constexpr uint8 AP_GETCHOICE = 22;
	constexpr uint8 AP_LOGINOK = 23;
	constexpr uint8 AP_LOGINFAILED = 24;
	constexpr uint8 AP_GAME = 25;
	constexpr uint8 AP_ACCOUNTUSED = 27;
	constexpr uint8 AP_TOOMANYLOGINS = 28;
	constexpr uint8 AP_TIMEOUT = 29;
	constexpr uint8 AP_CREDITS = 30;
	constexpr uint8 AP_DOWNLOAD = 31;
	constexpr uint8 AP_MESSAGE = 34;
	constexpr uint8 AP_NOCHARACTERS = 37;

	// game mode (BP_*), every id in proto.h; most are handled by later milestones (docs/parity.md)
	constexpr uint8 BP_ECHO_PING = 1;
	constexpr uint8 BP_RESYNC = 2;
	constexpr uint8 BP_PING = 3;
	constexpr uint8 BP_SYSTEM = 6;
	constexpr uint8 BP_LOGOFF = 20;
	constexpr uint8 BP_WAIT = 21;
	constexpr uint8 BP_UNWAIT = 22;
	constexpr uint8 BP_CHANGE_PASSWORD = 23;
	constexpr uint8 BP_CHANGE_RESOURCE = 30;
	constexpr uint8 BP_SYS_MESSAGE = 31;
	constexpr uint8 BP_MESSAGE = 32;
	constexpr uint8 BP_SEND_PLAYER = 40;
	constexpr uint8 BP_SEND_STATS = 41;
	constexpr uint8 BP_SEND_ROOM_CONTENTS = 42;
	constexpr uint8 BP_SEND_OBJECT_CONTENTS = 43;
	constexpr uint8 BP_SEND_PLAYERS = 44;
	constexpr uint8 BP_SEND_CHARACTERS = 45;
	constexpr uint8 BP_USE_CHARACTER = 46;
	constexpr uint8 BP_DELETE_CHARACTER = 47;
	constexpr uint8 BP_NEW_CHARINFO = 48;
	constexpr uint8 BP_SEND_CHARINFO = 49;
	constexpr uint8 BP_SEND_SPELLS = 50;
	constexpr uint8 BP_SEND_SKILLS = 51;
	constexpr uint8 BP_SEND_STAT_GROUPS = 52;
	constexpr uint8 BP_SEND_ENCHANTMENTS = 53;
	constexpr uint8 BP_REQ_QUIT = 54;
	constexpr uint8 BP_SAY_BLOCKED = 55;
	constexpr uint8 BP_CHARINFO_OK = 56;
	constexpr uint8 BP_CHARINFO_NOT_OK = 57;
	constexpr uint8 BP_LOAD_MODULE = 58;
	constexpr uint8 BP_UNLOAD_MODULE = 59;
	constexpr uint8 BP_REQ_ADMIN = 60;
	constexpr uint8 BP_REQ_DM = 61;
	constexpr uint8 BP_EFFECT = 70;
	constexpr uint8 BP_MAIL = 80;
	constexpr uint8 BP_REQ_GET_MAIL = 81;
	constexpr uint8 BP_SEND_MAIL = 82;
	constexpr uint8 BP_DELETE_MAIL = 83;
	constexpr uint8 BP_DELETE_NEWS = 84;
	constexpr uint8 BP_REQ_ARTICLES = 85;
	constexpr uint8 BP_REQ_ARTICLE = 86;
	constexpr uint8 BP_POST_ARTICLE = 87;
	constexpr uint8 BP_REQ_LOOKUP_NAMES = 88;
	constexpr uint8 BP_ACTION = 90;
	constexpr uint8 BP_REQ_MOVE = 100;
	constexpr uint8 BP_REQ_TURN = 101;
	constexpr uint8 BP_REQ_GO = 102;
	constexpr uint8 BP_REQ_ATTACK = 103;
	constexpr uint8 BP_REQ_SHOOT = 104;
	constexpr uint8 BP_REQ_CAST = 105;
	constexpr uint8 BP_REQ_USE = 106;
	constexpr uint8 BP_REQ_UNUSE = 107;
	constexpr uint8 BP_REQ_APPLY = 108;
	constexpr uint8 BP_REQ_ACTIVATE = 109;
	constexpr uint8 BP_SAY_TO = 110;
	constexpr uint8 BP_SAY_GROUP = 111;
	constexpr uint8 BP_REQ_PUT = 112;
	constexpr uint8 BP_REQ_GET = 113;
	constexpr uint8 BP_REQ_GIVE = 114;
	constexpr uint8 BP_REQ_TAKE = 115;
	constexpr uint8 BP_REQ_LOOK = 116;
	constexpr uint8 BP_REQ_INVENTORY = 117;
	constexpr uint8 BP_REQ_DROP = 118;
	constexpr uint8 BP_REQ_HIDE = 119;
	constexpr uint8 BP_REQ_OFFER = 120;
	constexpr uint8 BP_ACCEPT_OFFER = 121;
	constexpr uint8 BP_CANCEL_OFFER = 122;
	constexpr uint8 BP_REQ_COUNTEROFFER = 123;
	constexpr uint8 BP_REQ_BUY = 124;
	constexpr uint8 BP_REQ_BUY_ITEMS = 125;
	constexpr uint8 BP_CHANGE_DESCRIPTION = 126;
	constexpr uint8 BP_REQ_INVENTORY_MOVE = 127;
	constexpr uint8 BP_PLAYER = 130;
	constexpr uint8 BP_STAT = 131;
	constexpr uint8 BP_STAT_GROUP = 132;
	constexpr uint8 BP_STAT_GROUPS = 133;
	constexpr uint8 BP_ROOM_CONTENTS = 134;
	constexpr uint8 BP_OBJECT_CONTENTS = 135;
	constexpr uint8 BP_PLAYERS = 136;
	constexpr uint8 BP_PLAYER_ADD = 137;
	constexpr uint8 BP_PLAYER_REMOVE = 138;
	constexpr uint8 BP_CHARACTERS = 139;
	constexpr uint8 BP_CHARINFO = 140;
	constexpr uint8 BP_SPELLS = 141;
	constexpr uint8 BP_SPELL_ADD = 142;
	constexpr uint8 BP_SPELL_REMOVE = 143;
	constexpr uint8 BP_SKILLS = 144;
	constexpr uint8 BP_SKILL_ADD = 145;
	constexpr uint8 BP_SKILL_REMOVE = 146;
	constexpr uint8 BP_ADD_ENCHANTMENT = 147;
	constexpr uint8 BP_REMOVE_ENCHANTMENT = 148;
	constexpr uint8 BP_QUIT = 149;
	constexpr uint8 BP_BACKGROUND = 150;
	constexpr uint8 BP_PLAYER_OVERLAY = 151;
	constexpr uint8 BP_ADD_BG_OVERLAY = 152;
	constexpr uint8 BP_REMOVE_BG_OVERLAY = 153;
	constexpr uint8 BP_CHANGE_BG_OVERLAY = 154;
	constexpr uint8 BP_USERCOMMAND = 155;
	constexpr uint8 BP_REQ_STAT_CHANGE = 156;
	constexpr uint8 BP_CHANGED_STATS = 157;
	constexpr uint8 BP_CHANGED_STATS_OK = 158;
	constexpr uint8 BP_CHANGED_STATS_NOT_OK = 159;
	constexpr uint8 BP_PASSWORD_OK = 160;
	constexpr uint8 BP_PASSWORD_NOT_OK = 161;
	constexpr uint8 BP_ADMIN = 162;
	constexpr uint8 BP_PLAY_WAVE = 170;
	constexpr uint8 BP_PLAY_MUSIC = 171;
	constexpr uint8 BP_PLAY_MIDI = 172;
	constexpr uint8 BP_STOP_WAVE = 173;
	constexpr uint8 BP_LOOK_NEWSGROUP = 180;
	constexpr uint8 BP_ARTICLES = 181;
	constexpr uint8 BP_ARTICLE = 182;
	constexpr uint8 BP_LOOKUP_NAMES = 190;
	constexpr uint8 BP_MOVE = 200;
	constexpr uint8 BP_TURN = 201;
	constexpr uint8 BP_SHOOT = 202;
	constexpr uint8 BP_USE = 203;
	constexpr uint8 BP_UNUSE = 204;
	constexpr uint8 BP_USE_LIST = 205;
	constexpr uint8 BP_SAID = 206;
	constexpr uint8 BP_LOOK = 207;
	constexpr uint8 BP_INVENTORY = 208;
	constexpr uint8 BP_INVENTORY_ADD = 209;
	constexpr uint8 BP_INVENTORY_REMOVE = 210;
	constexpr uint8 BP_OFFER = 211;
	constexpr uint8 BP_OFFER_CANCELED = 212;
	constexpr uint8 BP_OFFERED = 213;
	constexpr uint8 BP_COUNTEROFFER = 214;
	constexpr uint8 BP_COUNTEROFFERED = 215;
	constexpr uint8 BP_BUY_LIST = 216;
	constexpr uint8 BP_CREATE = 217;
	constexpr uint8 BP_REMOVE = 218;
	constexpr uint8 BP_CHANGE = 219;
	constexpr uint8 BP_LIGHT_AMBIENT = 220;
	constexpr uint8 BP_LIGHT_PLAYER = 221;
	constexpr uint8 BP_LIGHT_SHADING = 222;
	constexpr uint8 BP_SECTOR_MOVE = 223;
	constexpr uint8 BP_SECTOR_LIGHT = 224;
	constexpr uint8 BP_WALL_ANIMATE = 225;
	constexpr uint8 BP_SECTOR_ANIMATE = 226;
	constexpr uint8 BP_CHANGE_TEXTURE = 227;
	constexpr uint8 BP_INVALIDATE_DATA = 228;
	constexpr uint8 BP_RADIUS_SHOOT = 229;
	constexpr uint8 BP_REQ_DEPOSIT = 230;
	constexpr uint8 BP_WITHDRAWAL_LIST = 231;
	constexpr uint8 BP_REQ_WITHDRAWAL = 232;
	constexpr uint8 BP_REQ_WITHDRAWAL_ITEMS = 233;
	constexpr uint8 BP_XLAT_OVERRIDE = 234;
	constexpr uint8 BP_WALL_SCROLL = 235;
	constexpr uint8 BP_SECTOR_SCROLL = 236;
	constexpr uint8 BP_SET_VIEW = 237;
	constexpr uint8 BP_RESET_VIEW = 238;
	constexpr uint8 BP_SECTOR_CHANGE = 239;
	constexpr uint8 BP_REQ_GET_FROM_CONTAINER = 240;

	// user commands inside BP_USERCOMMAND (proto.h UC_*)
	constexpr uint8 UC_SEND_QUIT = 1;   // from the server: go back to the character list (after a suicide)
	constexpr uint8 UC_LOOK_PLAYER = 2;
	constexpr uint8 UC_REST = 5;
	constexpr uint8 UC_STAND = 6;
	constexpr uint8 UC_REQ_PREFERENCES = 7;
	constexpr uint8 UC_SUICIDE = 8;
	constexpr uint8 UC_SEND_PREFERENCES = 9;     // i32 CF_* flags
	constexpr uint8 UC_REQ_GUILDINFO = 10;
	constexpr uint8 UC_GUILDINFO = 11;           // from the server: the guild (MRNetRead::GuildInfo)
	constexpr uint8 UC_INVITE = 12;              // u32 player
	constexpr uint8 UC_EXILE = 13;               // u32 player
	constexpr uint8 UC_RENOUNCE = 14;
	constexpr uint8 UC_ABDICATE = 15;            // u32 player
	constexpr uint8 UC_VOTE = 16;                // u32 player
	constexpr uint8 UC_SET_RANK = 17;            // u32 player, u8 rank
	constexpr uint8 UC_GUILD_ASK = 18;           // from the server: i32 cost, i32 secret cost
	constexpr uint8 UC_GUILD_CREATE = 19;        // name, 5 x (male rank, female rank), password, u8 secret
	constexpr uint8 UC_DISBAND = 20;
	constexpr uint8 UC_REQ_GUILD_LIST = 21;
	constexpr uint8 UC_GUILD_LIST = 22;          // from the server (MRNetRead::GuildList)
	constexpr uint8 UC_MAKE_ALLIANCE = 23;       // u32 guild
	constexpr uint8 UC_END_ALLIANCE = 24;
	constexpr uint8 UC_MAKE_ENEMY = 25;
	constexpr uint8 UC_END_ENEMY = 26;
	constexpr uint8 UC_GUILD_SET_PASSWORD = 30;  // the guild hall's password
	constexpr uint8 UC_RECEIVE_PREFERENCES = 34; // from the server: i32 CF_* flags
	constexpr uint8 UC_DEPOSIT = 35;   // i32 shillings, to a banker in the room
	constexpr uint8 UC_WITHDRAW = 36;  // i32 shillings
	constexpr uint8 UC_BALANCE = 37;
	constexpr uint8 UC_APPEAL = 40;              // a string, to the guides
	constexpr uint8 UC_REQ_TIME = 60;            // the server answers with Meridian's date and hour

	// the server-kept game options (proto.h CF_*: UC_REQ_PREFERENCES, UC_SEND_PREFERENCES)
	constexpr uint32 CF_SAFETY_OFF = 0x0001;
	constexpr uint32 CF_TEMPSAFE = 0x0002;
	constexpr uint32 CF_GROUPING = 0x0004;
	constexpr uint32 CF_AUTOLOOT = 0x0008;
	constexpr uint32 CF_AUTOCOMBINE = 0x0010;
	constexpr uint32 CF_BAGS = 0x0020;
	constexpr uint32 CF_SPELLPOWER = 0x0040;

	// what a guild member may do (merintr guild.h GC_*: UC_GUILDINFO's flags)
	constexpr uint32 GC_INVITE = 0x0001;
	constexpr uint32 GC_EXILE = 0x0002;
	constexpr uint32 GC_RENOUNCE = 0x0004;
	constexpr uint32 GC_VOTE = 0x0020;
	constexpr uint32 GC_ABDICATE = 0x0040;
	constexpr uint32 GC_MAKE_ALLIANCE = 0x0100;
	constexpr uint32 GC_END_ALLIANCE = 0x0200;
	constexpr uint32 GC_DECLARE_ENEMY = 0x0400;
	constexpr uint32 GC_END_ENEMY = 0x0800;
	constexpr uint32 GC_SET_RANK = 0x1000;
	constexpr uint32 GC_DISBAND = 0x2000;
	constexpr uint32 GC_ABANDON = 0x4000;
	constexpr int32 GuildRanks = 5;

	// BP_LOOK_NEWSGROUP's permission (news.h NEWS_*)
	constexpr uint8 NEWS_READ = 0x01;
	constexpr uint8 NEWS_POST = 0x02;

	// BP_ACTION's actions (merintr command.c UA_*): a mood, or an animation the server shows everyone
	constexpr uint8 UA_NORMAL = 1;
	constexpr uint8 UA_HAPPY = 2;
	constexpr uint8 UA_SAD = 3;
	constexpr uint8 UA_WRY = 4;
	constexpr uint8 UA_WAVE = 8;
	constexpr uint8 UA_POINT = 9;
	constexpr uint8 UA_DANCE = 10;

	/** Kod's times (mail, news) are Unix seconds less this (blakserv ccode.c C_GetTime: "Offset to Oct 2025"). */
	constexpr int64 KodTimeOffset = 1760000000;

	// enchantment types (BP_ADD_ENCHANTMENT, BP_SEND_ENCHANTMENTS; proto.h ENCHANT_*)
	constexpr uint8 ENCHANT_PLAYER = 1;
	constexpr uint8 ENCHANT_ROOM = 2;

	// BP_LOOK description flags (proto.h DF_*)
	constexpr uint8 DF_EDITABLE = 0x01;
	constexpr uint8 DF_INSCRIBED = 0x02;

	// drawing effects (proto.h DRAWFX_*): an object's DrawEffect
	constexpr uint8 DRAWFX_TRANSLUCENT25 = 0x01;
	constexpr uint8 DRAWFX_TRANSLUCENT50 = 0x02;
	constexpr uint8 DRAWFX_TRANSLUCENT75 = 0x03;
	constexpr uint8 DRAWFX_BLACK = 0x04;
	constexpr uint8 DRAWFX_INVISIBLE = 0x05;
	constexpr uint8 DRAWFX_DITHERINVIS = 0x07;
	constexpr uint8 DRAWFX_DITHERGREY = 0x0B;

	// minimap dots (proto.h MM_*)
	constexpr uint32 MM_PLAYER = 0x0001;
	constexpr uint32 MM_ENEMY = 0x0002;
	constexpr uint32 MM_FRIEND = 0x0004;
	constexpr uint32 MM_GUILDMATE = 0x0008;
	constexpr uint32 MM_MONSTER = 0x0020;
	constexpr uint32 MM_NPC = 0x0040;
	constexpr uint32 MM_MINION_SELF = 0x0100;
	constexpr uint32 MM_BOSS = 0x0800;

	// say types (proto.h SAY_*)
	constexpr uint8 SAY_NORMAL = 1;
	constexpr uint8 SAY_YELL = 2;
	constexpr uint8 SAY_EVERYONE = 3;
	constexpr uint8 SAY_GROUP = 4;     // a tell (BP_SAY_GROUP)
	constexpr uint8 SAY_RESOURCE = 5;  // an NPC or an object
	constexpr uint8 SAY_EMOTE = 6;
	constexpr uint8 SAY_MESSAGE = 7;
	constexpr uint8 SAY_DM = 9;
	constexpr uint8 SAY_GUILD = 10;

	// object flags (proto.h OF_*)
	constexpr uint32 OF_DISPLAY_NAME = 0x00000001;
	constexpr uint32 OF_SIGN = 0x00000002;
	constexpr uint32 OF_PLAYER = 0x00000004;
	constexpr uint32 OF_ATTACKABLE = 0x00000008;
	constexpr uint32 OF_GETTABLE = 0x00000010;
	constexpr uint32 OF_CONTAINER = 0x00000020;
	constexpr uint32 OF_NOEXAMINE = 0x00000040;
	constexpr uint32 OF_ITEM_MAGIC = 0x00000080;
	constexpr uint32 OF_HANGING = 0x00000100;
	constexpr uint32 OF_OFFERABLE = 0x00000200;
	constexpr uint32 OF_BUYABLE = 0x00000400;
	constexpr uint32 OF_ACTIVATABLE = 0x00000800;
	constexpr uint32 OF_APPLYABLE = 0x00001000;
	constexpr uint32 OF_NPC = 0x00002000;

	// an object's light (proto.h LIGHT_FLAG_*): no intensity or colour follow LIGHT_FLAG_NONE
	constexpr uint16 LIGHT_FLAG_NONE = 0;

	// animation records (proto.h ANIMATE_*)
	constexpr uint8 ANIMATE_NONE = 1;
	constexpr uint8 ANIMATE_CYCLE = 2;
	constexpr uint8 ANIMATE_ONCE = 3;
	constexpr uint8 ANIMATE_FLOOR_LIFT = 4;    // BP_SECTOR_MOVE's kind
	constexpr uint8 ANIMATE_CEILING_LIFT = 5;
	constexpr uint8 ANIMATE_TRANSLATION = 9;
	constexpr uint8 ANIMATE_EFFECT = 10;

	// first-person pictures (BP_PLAYER_OVERLAY): slots (blakston.khd PWO_*) and screen hotspots (HS_NW..HS_CENTER)
	constexpr uint32 PWO_LEFT_HAND = 1;
	constexpr uint32 PWO_RIGHT_HAND = 2;
	constexpr uint8 HS_SE = 5;
	constexpr uint8 HS_SW = 7;

	// BP_CHANGE_TEXTURE flags (proto.h CTF_*): which surfaces with the id change
	constexpr uint8 CTF_ABOVEWALL = 0x01;
	constexpr uint8 CTF_NORMALWALL = 0x02;
	constexpr uint8 CTF_BELOWWALL = 0x04;
	constexpr uint8 CTF_FLOOR = 0x08;
	constexpr uint8 CTF_CEILING = 0x10;
	// BP_SECTOR_CHANGE: keep that value (roomanim.h CHANGE_OVERRIDE)
	constexpr uint8 CHANGE_OVERRIDE = 4;

	// BP_PLAY_WAVE flags (proto.h SF_*)
	constexpr uint8 SF_LOOP = 0x01;          // until the player leaves the room
	constexpr uint8 SF_RANDOM_PITCH = 0x02;  // (the original client ignores it)
	constexpr uint8 SF_RANDOM_PLACE = 0x04;  // Kod chose a random square (the "random sounds" setting)

	// BP_REQ_ATTACK's kind (proto.h ATTACK_*)
	constexpr uint8 ATTACK_NORMAL = 1;

	// screen effects (BP_EFFECT; proto.h EFFECT_*, clientd3d effect.c PerformEffect)
	constexpr uint16 EFFECT_INVERT = 1;        // i32 ms
	constexpr uint16 EFFECT_SHAKE = 2;         // i32 ms
	constexpr uint16 EFFECT_PARALYZE = 3;
	constexpr uint16 EFFECT_RELEASE = 4;
	constexpr uint16 EFFECT_BLIND = 5;
	constexpr uint16 EFFECT_SEE = 6;
	constexpr uint16 EFFECT_PAIN = 7;          // i32 ms
	constexpr uint16 EFFECT_BLUR = 8;          // i32 ms
	constexpr uint16 EFFECT_RAINING = 9;
	constexpr uint16 EFFECT_SNOWING = 10;
	constexpr uint16 EFFECT_CLEARWEATHER = 11;
	constexpr uint16 EFFECT_SAND = 12;
	constexpr uint16 EFFECT_CLEARSAND = 13;
	constexpr uint16 EFFECT_WAVER = 14;        // i32 ms
	constexpr uint16 EFFECT_FLASHXLAT = 15;    // i32 ms, i32 xlat
	constexpr uint16 EFFECT_WHITEOUT = 16;     // i32 ms
	constexpr uint16 EFFECT_XLATOVERRIDE = 17; // i32 xlat (0 = off)
	constexpr uint16 EFFECT_FIREWORKS = 18;

	// projectiles (BP_SHOOT, BP_RADIUS_SHOOT; clientd3d project.h)
	constexpr uint16 PROJ_FLAG_FOLLOWGROUND = 0x0001;

	// movement speeds sent with BP_REQ_MOVE (walk and run, as the original client)
	constexpr uint8 SPEED_WALK = 25;
	constexpr uint8 SPEED_RUN = 55;

	/** Kod position units per grid square, and where row/col 1 starts (blakserv sends row*64+fine). */
	constexpr int32 KodFineness = 64;

	constexpr int32 HeaderBytes = 7;
	constexpr int32 MaxBody = 32 * 1024;

	/** An object id without its 4-bit tag (the top bits mark number items such as shillings). */
	inline uint32 PlainId(uint32 Id) { return Id & 0x0FFFFFFF; }
	inline bool IsNumberId(uint32 Id) { return (Id >> 28) == 1; }
	/** A number item's id as the client sends it (CLIENT_TAG_NUMBER in the top bits); the amount follows. */
	inline uint32 NumberId(uint32 Id) { return PlainId(Id) | (1u << 28); }
}

/** Appends little-endian fields to a message body. */
class MERIDIANREMASTERED_API FMRWriter
{
public:
	explicit FMRWriter(uint8 Type) { Bytes.Add(Type); }
	FMRWriter& U8(uint8 V) { Bytes.Add(V); return *this; }
	FMRWriter& U16(uint16 V);
	FMRWriter& U32(uint32 V);
	FMRWriter& I32(int32 V) { return U32(static_cast<uint32>(V)); }
	/** u16 length + Latin-1 bytes (characters above 255 become '?'). */
	FMRWriter& Str(const FString& S);
	/** u16 length + raw bytes. */
	FMRWriter& Raw(const TArray<uint8>& Data);
	TArray<uint8> Bytes;
};

/** Reads little-endian fields; past the end it returns zeros and remembers the error. */
class MERIDIANREMASTERED_API FMRReader
{
public:
	FMRReader(const uint8* InData, int32 InNum, int32 InPos = 0) : Data(InData), Num(InNum), Pos(InPos) {}
	explicit FMRReader(const TArray<uint8>& Body, int32 InPos = 1) : Data(Body.GetData()), Num(Body.Num()), Pos(InPos) {}

	uint8 U8();
	/** The next byte without consuming it (0 at the end). */
	uint8 Peek() const { return Pos < Num ? Data[Pos] : 0; }
	uint16 U16();
	uint32 U32();
	int32 I32() { return static_cast<int32>(U32()); }
	/** A u16-length Latin-1 string. */
	FString Str();
	void Skip(int32 Bytes);
	int32 Remaining() const { return FMath::Max(0, Num - Pos); }
	bool IsOk() const { return !bError; }
	bool AtEnd() const { return Pos >= Num; }

private:
	bool Need(int32 Bytes);
	const uint8* Data = nullptr;
	int32 Num = 0;
	int32 Pos = 0;
	bool bError = false;
};

/** A frame's header fields and body. */
struct FMRFrame
{
	uint16 Check = 0;  // CRC or security word
	uint8 Epoch = 0;
	TArray<uint8> Body;
};

/** Splits a byte stream into frames (the gateway forwards TCP chunks, not frames). */
class MERIDIANREMASTERED_API FMRFrameDecoder
{
public:
	void Append(const uint8* InData, int32 InNum) { Buffer.Append(InData, InNum); }
	/** The next whole frame, if one has arrived. False with IsBroken() on a corrupt header. */
	bool Next(FMRFrame& Out);
	bool IsBroken() const { return bBroken; }
	void Reset() { Buffer.Reset(); bBroken = false; }

private:
	TArray<uint8> Buffer;
	bool bBroken = false;
};

namespace MRProto
{
	/** CRC-32 (the reflected 0xEDB88320 polynomial, as blakserv util/crc.c). */
	MERIDIANREMASTERED_API uint32 Crc32(const uint8* Data, int32 Num);
	inline uint16 Crc16(const TArray<uint8>& Body) { return static_cast<uint16>(Crc32(Body.GetData(), Body.Num()) & 0xFFFF); }
	/** Header + body. */
	MERIDIANREMASTERED_API TArray<uint8> EncodeFrame(const TArray<uint8>& Body, uint16 Check, uint8 Epoch);
	/** MD5 of the password with zero bytes made 1 (util/md5.c MDString), as AP_LOGIN sends it. */
	MERIDIANREMASTERED_API TArray<uint8> PasswordDigest(const FString& Password);
	/** Latin-1 bytes of a string (characters above 255 become '?'). */
	MERIDIANREMASTERED_API TArray<uint8> Latin1(const FString& S);
}

/**
 * The client's game-mode security word (blakserv game.c GameRandomStreamsStep): five linear
 * congruential streams seeded by AP_GETCHOICE, stepped once per message sent, mixed with the
 * message's length, sign-extended type byte and CRC. One wrong word and the server hangs up.
 */
class MERIDIANREMASTERED_API FMRSecurityStreams
{
public:
	void Seed(const uint32 (&InSeeds)[5]) { FMemory::Memcpy(Seeds, InSeeds, sizeof(Seeds)); }
	/** The word for the next message (steps the streams). */
	uint16 Next(const TArray<uint8>& Body);

private:
	uint32 Seeds[5] = {};
};

/**
 * The server's type-byte scrambling (blakserv commcli.c SecurePacketBufferList): every packet's
 * first byte is XORed with a token, which then advances by the next character of the "redbook"
 * string. BP_ECHO_PING sets a new token and names the redbook resource.
 */
class MERIDIANREMASTERED_API FMRServerToken
{
public:
	/** Unscramble a received body's type byte, then advance. */
	void Decode(TArray<uint8>& Body);
	/** After a BP_ECHO_PING: the new token and redbook text (empty: no sliding). */
	void Rekey(uint8 TokenByte, const TArray<uint8>& InRedbook);
	void Reset() { Token = 0; Redbook.Reset(); Pos = 0; }

	/** blakserv's redbook when no resource is configured (game.c GetSecurityRedbook). */
	static const char* DefaultRedbook() { return "BLAKSTON: Greenwich Q Zjiria"; }

private:
	uint32 Token = 0;
	TArray<uint8> Redbook;
	int32 Pos = 0;
};
