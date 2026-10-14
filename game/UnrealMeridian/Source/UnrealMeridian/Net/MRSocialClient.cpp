#include "Net/MRSocialClient.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "IWebSocket.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMeridian.h"
#include "WebSocketsModule.h"

THIRD_PARTY_INCLUDES_START
#define UI UI_ST
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#undef UI
THIRD_PARTY_INCLUDES_END

namespace
{
	/** socialProtocol.ts HELLO_PREFIX */
	const TCHAR* HelloPrefix = TEXT("meridian-shards-social");
	constexpr double MaxBackoff = 60.0;

	TArray<uint8> Utf8(const FString& S)
	{
		const FTCHARToUTF8 U(*S);
		return TArray<uint8>(reinterpret_cast<const uint8*>(U.Get()), U.Length());
	}

	EC_KEY* KeyFromDer(const TArray<uint8>& Der)
	{
		const unsigned char* P = Der.GetData();
		return Der.Num() ? d2i_ECPrivateKey(nullptr, &P, Der.Num()) : nullptr;
	}
}

FMRSocialClient::FMRSocialClient(const FString& InUrl, const FString& InServer, const FString& InName, const FString& InOrigin)
	: Url(InUrl), Server(InServer), Name(InName), Origin(InOrigin)
{
}

FMRSocialClient::~FMRSocialClient()
{
	Close();
}

FString FMRSocialClient::HelloText(const FString& Nonce, const FString& InServer, const FString& InName)
{
	return FString::Printf(TEXT("%s\n%s\n%s\n%s"), HelloPrefix, *Nonce, *InServer, *InName);
}

FString FMRSocialClient::Base64Url(const TArray<uint8>& Bytes)
{
	return FBase64::Encode(Bytes, EBase64Mode::UrlSafe).Replace(TEXT("="), TEXT(""));
}

bool FMRSocialClient::EnsureKey(TArray<uint8>& Der, TArray<uint8>& OutPublic)
{
	EC_KEY* Key = KeyFromDer(Der);
	if (!Key)
	{
		Key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
		if (!Key || EC_KEY_generate_key(Key) != 1)
		{
			EC_KEY_free(Key);
			return false;
		}
		const int Len = i2d_ECPrivateKey(Key, nullptr);
		Der.SetNumUninitialized(FMath::Max(0, Len));
		unsigned char* P = Der.GetData();
		if (Len <= 0 || i2d_ECPrivateKey(Key, &P) != Len)
		{
			EC_KEY_free(Key);
			return false;
		}
	}
	// the raw public point, uncompressed (0x04 x y): what WebCrypto exports as "raw"
	OutPublic.SetNumZeroed(65);
	const size_t N = EC_POINT_point2oct(EC_KEY_get0_group(Key), EC_KEY_get0_public_key(Key), POINT_CONVERSION_UNCOMPRESSED, OutPublic.GetData(), 65, nullptr);
	EC_KEY_free(Key);
	return N == 65;
}

bool FMRSocialClient::Sign(const TArray<uint8>& Der, const FString& Text, TArray<uint8>& OutSig)
{
	EC_KEY* Key = KeyFromDer(Der);
	if (!Key)
	{
		return false;
	}
	const TArray<uint8> Msg = Utf8(Text);
	uint8 Digest[SHA256_DIGEST_LENGTH];
	SHA256(Msg.GetData(), Msg.Num(), Digest);
	ECDSA_SIG* Sig = ECDSA_do_sign(Digest, SHA256_DIGEST_LENGTH, Key);
	EC_KEY_free(Key);
	if (!Sig)
	{
		return false;
	}
	const BIGNUM* R = nullptr;
	const BIGNUM* S = nullptr;
	ECDSA_SIG_get0(Sig, &R, &S);
	// WebCrypto's ECDSA signature: r and s, 32 bytes each, big-endian
	OutSig.SetNumZeroed(64);
	const bool bOk = BN_bn2binpad(R, OutSig.GetData(), 32) == 32 && BN_bn2binpad(S, OutSig.GetData() + 32, 32) == 32;
	ECDSA_SIG_free(Sig);
	return bOk;
}

bool FMRSocialClient::Verify(const TArray<uint8>& Public, const FString& Text, const TArray<uint8>& SigBytes)
{
	if (Public.Num() != 65 || SigBytes.Num() != 64)
	{
		return false;
	}
	EC_KEY* Key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	EC_POINT* Point = Key ? EC_POINT_new(EC_KEY_get0_group(Key)) : nullptr;
	bool bOk = Point && EC_POINT_oct2point(EC_KEY_get0_group(Key), Point, Public.GetData(), 65, nullptr) == 1 && EC_KEY_set_public_key(Key, Point) == 1;
	ECDSA_SIG* Sig = ECDSA_SIG_new();
	if (bOk && Sig)
	{
		ECDSA_SIG_set0(Sig, BN_bin2bn(SigBytes.GetData(), 32, nullptr), BN_bin2bn(SigBytes.GetData() + 32, 32, nullptr));
		const TArray<uint8> Msg = Utf8(Text);
		uint8 Digest[SHA256_DIGEST_LENGTH];
		SHA256(Msg.GetData(), Msg.Num(), Digest);
		bOk = ECDSA_do_verify(Digest, SHA256_DIGEST_LENGTH, Sig, Key) == 1;
	}
	ECDSA_SIG_free(Sig);
	EC_POINT_free(Point);
	EC_KEY_free(Key);
	return bOk;
}

bool FMRSocialClient::LoadKey()
{
	// one key per character and server, kept: the relay knows us by it (Shards keeps its in IndexedDB)
	FString Safe = Server + TEXT("_") + Name;
	for (TCHAR& C : Safe)
	{
		if (!FChar::IsAlnum(C))
		{
			C = TEXT('_');
		}
	}
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MRNet"), TEXT("social"), Safe + TEXT(".key"));
	TArray<uint8> Der;
	FFileHelper::LoadFileToArray(Der, *Path, FILEREAD_Silent);
	const int32 Had = Der.Num();
	if (!EnsureKey(Der, PublicKey))
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRSocial: couldn't make a key"));
		return false;
	}
	if (!Had)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Der, *Path);
	}
	KeyDer = MoveTemp(Der);
	return true;
}

void FMRSocialClient::Connect()
{
	if (Socket.IsValid() || bClosed || Url.IsEmpty() || (!KeyDer.Num() && !LoadKey()))
	{
		return;
	}
	FWebSocketsModule& Module = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	TMap<FString, FString> Headers;
	if (!Origin.IsEmpty())
	{
		Headers.Add(TEXT("Origin"), Origin);
	}
	Socket = Module.CreateWebSocket(Url, TArray<FString>(), Headers);
	TWeakPtr<FMRSocialClient> Weak = AsShared();
	auto OnGame = [Weak](TFunction<void(FMRSocialClient&)> Fn)
	{
		AsyncTask(ENamedThreads::GameThread, [Weak, Fn = MoveTemp(Fn)]()
		{
			if (TSharedPtr<FMRSocialClient> Self = Weak.Pin())
			{
				Fn(*Self);
			}
		});
	};
	Socket->OnMessage().AddLambda([OnGame](const FString& Text) { OnGame([Text](FMRSocialClient& C) { C.Receive(Text); }); });
	Socket->OnConnectionError().AddLambda([OnGame](const FString& Error)
	{
		OnGame([Error](FMRSocialClient& C)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRSocial: can't reach the relay: %s"), *Error);
			C.Socket.Reset();
			C.Again();
		});
	});
	Socket->OnClosed().AddLambda([OnGame](int32 Code, const FString& Reason, bool)
	{
		OnGame([Code, Reason](FMRSocialClient& C)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRSocial: closed (%d %s)"), Code, *Reason);
			C.Socket.Reset();
			C.bWelcome = false;
			if (C.OnArena.IsBound())
			{
				C.OnArena.Execute(nullptr);
			}
			// another window took this character's key, or the relay refused our signature: stay away
			if (Code == 4003 || Code == 4004)
			{
				C.bClosed = true;
				return;
			}
			C.Again();
		});
	});
	UE_LOG(LogMeridian, Log, TEXT("MRSocial: connecting to %s as %s"), *Url, *Name);
	Socket->Connect();
}

void FMRSocialClient::Again()
{
	if (bClosed)
	{
		return;
	}
	const float Wait = float(Backoff * FMath::FRandRange(0.75, 1.25));
	Backoff = FMath::Min(MaxBackoff, Backoff * 2.0);
	TWeakPtr<FMRSocialClient> Weak = AsShared();
	FTSTicker::GetCoreTicker().RemoveTicker(Retry);
	Retry = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
	{
		if (TSharedPtr<FMRSocialClient> Self = Weak.Pin())
		{
			Self->Connect();
		}
		return false;
	}), Wait);
}

void FMRSocialClient::Close()
{
	bClosed = true;
	FTSTicker::GetCoreTicker().RemoveTicker(Retry);
	if (Socket.IsValid())
	{
		Socket->OnMessage().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnClosed().Clear();
		Socket->Close();
		Socket.Reset();
	}
	bWelcome = false;
}

void FMRSocialClient::Raw(const TSharedRef<FJsonObject>& Msg)
{
	if (!Socket.IsValid() || !Socket->IsConnected())
	{
		return;
	}
	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Msg, Writer);
	Socket->Send(Text);
}

void FMRSocialClient::WatchArena(const FString& Room, double InHour)
{
	if (Room == ArenaRoom)
	{
		SetHour(InHour);
		return;
	}
	if (!ArenaRoom.IsEmpty() && bWelcome)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), TEXT("arenaLeave"));
		Raw(M);
	}
	ArenaRoom = Room;
	Hour = InHour;
	if (!Room.IsEmpty() && bWelcome)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), TEXT("arenaWatch"));
		M->SetStringField(TEXT("room"), Room);
		Hour >= 0.0 ? M->SetNumberField(TEXT("hour"), FMath::FloorToInt(Hour)) : M->SetField(TEXT("hour"), MakeShared<FJsonValueNull>());
		Raw(M);
	}
}

void FMRSocialClient::SetHour(double InHour)
{
	if (FMath::FloorToInt(InHour) == FMath::FloorToInt(Hour))
	{
		return;
	}
	Hour = InHour;
	if (!ArenaRoom.IsEmpty() && bWelcome && Hour >= 0.0)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), TEXT("arenaHour"));
		M->SetNumberField(TEXT("hour"), FMath::FloorToInt(Hour));
		Raw(M);
	}
}

void FMRSocialClient::Receive(const FString& Text)
{
	TSharedPtr<FJsonObject> M;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), M) || !M.IsValid())
	{
		return;
	}
	const FString T = M->GetStringField(TEXT("t"));
	if (T == TEXT("challenge"))
	{
		TArray<uint8> Sig;
		if (!Sign(KeyDer, HelloText(M->GetStringField(TEXT("nonce")), Server, Name), Sig))
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRSocial: couldn't sign the hello"));
			return;
		}
		TSharedRef<FJsonObject> Hello = MakeShared<FJsonObject>();
		Hello->SetStringField(TEXT("t"), TEXT("hello"));
		Hello->SetStringField(TEXT("key"), Base64Url(PublicKey));
		Hello->SetStringField(TEXT("server"), Server);
		Hello->SetStringField(TEXT("name"), Name);
		Hello->SetStringField(TEXT("sig"), Base64Url(Sig));
		Hello->SetBoolField(TEXT("visible"), false);  // only watching the arena: no friends' presence
		Hello->SetArrayField(TEXT("watch"), TArray<TSharedPtr<FJsonValue>>());
		Raw(Hello);
	}
	else if (T == TEXT("welcome"))
	{
		bWelcome = true;
		Backoff = 2.0;
		UE_LOG(LogMeridian, Log, TEXT("MRSocial: the relay knows us (%s)"), *Name);
		const FString Room = ArenaRoom;
		ArenaRoom.Reset();
		WatchArena(Room, Hour);
	}
	else if (T == TEXT("arenaState") || T == TEXT("arenaTick"))
	{
		if (!ArenaRoom.IsEmpty() && OnArena.IsBound())
		{
			OnArena.Execute(M);
		}
	}
	else if (T == TEXT("arenaOff"))
	{
		UE_LOG(LogMeridian, Log, TEXT("MRSocial: the relay runs no arena"));
		if (OnArena.IsBound())
		{
			OnArena.Execute(nullptr);
		}
	}
}
