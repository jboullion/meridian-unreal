#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

class FJsonObject;

/**
 * The server's game files (docs/adr/0012-client-parity-and-world-coverage.md): anything its
 * manifest.json lists (rooms, bitmaps, sounds), downloaded on demand from its assets URL into
 * Saved/MRNet/<server>/assets/ and checked against the manifest's hash (the first 16 hex digits of
 * the file's SHA-1). Files come from the server we play on, so they always match it.
 *
 * Names are the manifest's: lower case, no folders ("raza.roo", "grd00101.bgf"). Lives on the game thread.
 */
class MERIDIANREMASTERED_API FMRAssetCache : public TSharedFromThis<FMRAssetCache>
{
public:
	using FDone = TFunction<void(bool bOk, const TArray<uint8>& Bytes)>;

	/** Where files come from and go to. Forgets the manifest and cancels downloads. */
	void Configure(const FString& InBaseUrl, const FString& InDir);
	/** manifest.json's "files" object: name -> {size, hash}. */
	void SetManifest(const TSharedPtr<FJsonObject>& Files);
	bool HasManifest() const { return Hashes.Num() > 0; }
	int32 NumListed() const { return Hashes.Num(); }
	bool IsListed(const FString& Name) const { return Hashes.Contains(Name.ToLower()); }

	/** The file if it is cached and matches the manifest; never downloads. */
	bool Load(const FString& Name, TArray<uint8>& Out);
	/** The file, downloaded first if needed. Done runs on the game thread, maybe before this returns. */
	void Fetch(const FString& Name, FDone Done);
	/** Drop queued downloads and their callbacks (logged off, another server). */
	void CancelAll();
	int32 NumPending() const { return Queue.Num() + Active.Num(); }

	/** The manifest's hash of some bytes: the first 16 hex digits of their SHA-1, lower case. */
	static FString HashBytes(const TArray<uint8>& Bytes);

private:
	struct FJob
	{
		FString Name;
		TArray<FDone> Waiting;
	};

	FString PathOf(const FString& Name) const;
	void Pump();
	void OnDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk, FString Name);
	void Finish(const FString& Name, bool bOk, const TArray<uint8>& Bytes);

	FString BaseUrl;
	FString Dir;
	/** name -> the manifest's hash */
	TMap<FString, FString> Hashes;
	/** names checked against their hash since the manifest arrived */
	TSet<FString> Verified;
	TArray<FJob> Queue;
	TMap<FString, FJob> Active;
	TArray<FHttpRequestPtr> Requests;
	static constexpr int32 MaxParallel = 8;
};
