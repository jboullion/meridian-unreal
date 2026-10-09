#pragma once

#include "CoreMinimal.h"
#include "Net/MRProtocol.h"

class IWebSocket;

/**
 * One connection to a Meridian server (blakserv) through a WebSocket gateway that passes the TCP
 * byte stream through unchanged (docs/adr/0010-meridian-servers.md).
 *
 * Handles the login exchange itself (AP_GETLOGIN -> AP_LOGIN -> AP_LOGINOK / AP_GETCHOICE ->
 * AP_REQ_GAME -> AP_GAME) and, in game mode, the security word on every message sent, the
 * type-byte token on every message received, the echo ping re-key and the 5 s keep-alive ping.
 * Everything else is handed to OnMessage. Lives on the game thread.
 *
 * Leaving the game (BP_QUIT) puts the session back at the server's menu: it sends a new
 * AP_GETCHOICE, we answer AP_REQ_GAME, and the character list follows (blakserv game.c
 * GameProtocolParse, synched.c SynchedDoMenu).
 *
 * A broken stream ends the session. blakserv's resync handshake (BP_RESYNC, then a beacon string)
 * can't complete: game.c GameSyncInputChar compares a signed char with the beacon's byte 255, so it
 * never gets past the second byte (docs/research/blakserv-protocol.md, "Session").
 */
class UNREALMERIDIAN_API FMRConnection : public TSharedFromThis<FMRConnection>
{
public:
	enum class EState : uint8 { Idle, Connecting, Login, Game, Closed };

	struct FLogin
	{
		FString User;
		FString Password;
		FString SecretKey;
	};

	~FMRConnection();

	/** A game-mode message (type byte first), already unscrambled. */
	TFunction<void(const TArray<uint8>& Body)> OnMessage;
	/** Login succeeded: the server switched to game mode. */
	TFunction<void()> OnGameMode;
	/** The connection ended; Error is what to tell the player (empty for a normal logoff). */
	TFunction<void(const FString& Error)> OnClosed;
	/** Bytes of a resource by id (the redbook for the security token). */
	TFunction<TArray<uint8>(uint32 Id)> ResourceBytes;

	/** Open Url (ws:// or wss://) and log in. Origin is sent for the gateway's allowlist. */
	void Connect(const FString& Url, const FString& Origin, const FLogin& InLogin);
	/** Send a game-mode message (dropped unless in game mode). Order matters: never reorder. */
	void Send(const TArray<uint8>& Body);
	void Send(const FMRWriter& W) { Send(W.Bytes); }
	/** Close without an error (logoff). */
	void Close();
	/** Keep-alive pings; call every frame. */
	void Tick(float DeltaSeconds);

	EState GetState() const { return State; }
	bool IsInGame() const { return State == EState::Game; }

private:
	void OnBytes(const void* Data, SIZE_T Size);
	void HandleFrame(FMRFrame& Frame);
	void HandleLogin(const TArray<uint8>& Body);
	void SendLogin(const TArray<uint8>& Body);
	void SendRaw(const TArray<uint8>& Frame);
	void Fail(const FString& Error);

	TSharedPtr<IWebSocket> Socket;
	FLogin Login;
	EState State = EState::Idle;
	FMRFrameDecoder Decoder;
	FMRSecurityStreams Streams;
	FMRServerToken Token;
	uint8 Epoch = 0;
	float PingTimer = 0.f;
	bool bClosing = false;
};
