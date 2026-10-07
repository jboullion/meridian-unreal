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

	// game mode (BP_*)
	constexpr uint8 BP_ECHO_PING = 1;
	constexpr uint8 BP_RESYNC = 2;
	constexpr uint8 BP_PING = 3;
	constexpr uint8 BP_SYSTEM = 6;
	constexpr uint8 BP_WAIT = 21;
	constexpr uint8 BP_UNWAIT = 22;
	constexpr uint8 BP_CHANGE_RESOURCE = 30;
	constexpr uint8 BP_SYS_MESSAGE = 31;
	constexpr uint8 BP_MESSAGE = 32;
	constexpr uint8 BP_SEND_CHARACTERS = 45;
	constexpr uint8 BP_USE_CHARACTER = 46;
	constexpr uint8 BP_NEW_CHARINFO = 48;
	constexpr uint8 BP_REQ_QUIT = 54;
	constexpr uint8 BP_CHARINFO_OK = 56;
	constexpr uint8 BP_CHARINFO_NOT_OK = 57;
	constexpr uint8 BP_LOAD_MODULE = 58;
	constexpr uint8 BP_REQ_MOVE = 100;
	constexpr uint8 BP_REQ_TURN = 101;
	constexpr uint8 BP_REQ_GO = 102;
	constexpr uint8 BP_SAY_TO = 110;
	constexpr uint8 BP_PLAYER = 130;
	constexpr uint8 BP_ROOM_CONTENTS = 134;
	constexpr uint8 BP_PLAYERS = 136;
	constexpr uint8 BP_PLAYER_ADD = 137;
	constexpr uint8 BP_PLAYER_REMOVE = 138;
	constexpr uint8 BP_CHARACTERS = 139;
	constexpr uint8 BP_QUIT = 149;
	constexpr uint8 BP_MOVE = 200;
	constexpr uint8 BP_TURN = 201;
	constexpr uint8 BP_SAID = 206;
	constexpr uint8 BP_CREATE = 217;
	constexpr uint8 BP_REMOVE = 218;
	constexpr uint8 BP_CHANGE = 219;

	// say types (proto.h SAY_*)
	constexpr uint8 SAY_NORMAL = 1;
	constexpr uint8 SAY_YELL = 2;
	constexpr uint8 SAY_EVERYONE = 3;
	constexpr uint8 SAY_EMOTE = 6;

	// object flags (proto.h OF_*)
	constexpr uint32 OF_PLAYER = 0x00000004;
	constexpr uint32 OF_ATTACKABLE = 0x00000008;
	constexpr uint32 OF_NPC = 0x00002000;

	// animation records (proto.h ANIMATE_*)
	constexpr uint8 ANIMATE_NONE = 1;
	constexpr uint8 ANIMATE_CYCLE = 2;
	constexpr uint8 ANIMATE_ONCE = 3;
	constexpr uint8 ANIMATE_TRANSLATION = 9;
	constexpr uint8 ANIMATE_EFFECT = 10;

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
