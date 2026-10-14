#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class IWebSocket;
class FJsonObject;

/**
 * A client of Meridian Shards' relay (its gateway's /social; Shards docs/adr/0005-friends.md and 0007-arena.md,
 * ours), for the arena's fights (docs/adr/0013-ambient-life.md). The relay runs one arena per game server and
 * sends everyone watching the same fights.
 *
 * - The relay knows a character by an ECDSA P-256 key: ours is made with the engine's OpenSSL, once per
 *   character and server, and kept in Saved/MRNet/social/<server>_<name>.key.
 * - It sends a challenge; we answer with a hello signed over "meridian-shards-social\n<nonce>\n<server>\n<name>"
 *   (SHA-256, the raw r||s of 64 bytes, base64url, as WebCrypto signs), our raw public key (65 bytes) and
 *   who we are. "welcome" means it took it.
 * - arenaWatch (the room and our game hour), arenaHour, arenaLeave; it answers arenaState then arenaTick
 *   every 100 ms, or arenaOff when it runs no arena. Gone: it reconnects after a while, as Shards' client.
 * - Only the arena: no friends, pets or presence (hidden: "visible" false).
 */
class UNREALMERIDIAN_API FMRSocialClient : public TSharedFromThis<FMRSocialClient>
{
public:
	DECLARE_DELEGATE_OneParam(FOnArena, TSharedPtr<FJsonObject> /* arenaState/arenaTick, null: arenaOff */);

	FMRSocialClient(const FString& InUrl, const FString& InServer, const FString& InName, const FString& InOrigin);
	~FMRSocialClient();

	void Connect();
	void Close();
	bool IsOnline() const { return bWelcome; }
	/** Watch the arena of a room (empty: stop), telling the relay the game hour our sun shows (-1: not known) */
	void WatchArena(const FString& Room, double Hour);
	/** The game hour changed while we watch */
	void SetHour(double Hour);
	FOnArena OnArena;

	/** The hello's text (socialProtocol.ts helloText) */
	static FString HelloText(const FString& Nonce, const FString& Server, const FString& Name);
	/**
	 * A P-256 key: DER private key bytes in, or a new one made when empty (and returned in Der); the raw
	 * public point (65 bytes) out. False if OpenSSL can't.
	 */
	static bool EnsureKey(TArray<uint8>& Der, TArray<uint8>& OutPublic);
	/** ECDSA P-256 over SHA-256 of Text: r||s (64 bytes) */
	static bool Sign(const TArray<uint8>& Der, const FString& Text, TArray<uint8>& OutSig);
	/** ...and checked against a raw public point (tests) */
	static bool Verify(const TArray<uint8>& Public, const FString& Text, const TArray<uint8>& Sig);
	static FString Base64Url(const TArray<uint8>& Bytes);

private:
	FString Url, Server, Name, Origin;
	TSharedPtr<IWebSocket> Socket;
	TArray<uint8> KeyDer, PublicKey;
	bool bWelcome = false;
	bool bClosed = false;
	FString ArenaRoom;
	double Hour = -1.0;
	double Backoff = 2.0;
	FTSTicker::FDelegateHandle Retry;

	void Raw(const TSharedRef<FJsonObject>& Msg);
	void Receive(const FString& Text);
	void Again();
	bool LoadKey();
};
