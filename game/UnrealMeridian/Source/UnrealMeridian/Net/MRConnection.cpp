#include "Net/MRConnection.h"

#include "Async/Async.h"
#include "IWebSocket.h"
#include "UnrealMeridian.h"
#include "WebSocketsModule.h"

#define LOCTEXT_NAMESPACE "MRNet"

namespace
{
	constexpr float PingSeconds = 5.f;  // blakserv hangs up after 30 s without a message

	// what AP_LOGIN says about the client: the original's version 50.55 (anything below
	// blakserv's [Login] InvalidVersion is turned away) and a plausible machine
	constexpr uint8 VersionMajor = 50;
	constexpr uint8 VersionMinor = 55;
}

FMRConnection::~FMRConnection()
{
	if (Socket.IsValid())
	{
		Socket->OnConnected().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnClosed().Clear();
		Socket->OnRawMessage().Clear();
		Socket->Close();
	}
}

void FMRConnection::Connect(const FString& Url, const FString& Origin, const FLogin& InLogin)
{
	Login = InLogin;
	State = EState::Connecting;
	Decoder.Reset();
	Token.Reset();
	Epoch = 0;
	bClosing = false;

	FWebSocketsModule& Module = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	TMap<FString, FString> Headers;
	if (!Origin.IsEmpty())
	{
		Headers.Add(TEXT("Origin"), Origin);
	}
	Socket = Module.CreateWebSocket(Url, TArray<FString>{TEXT("binary")}, Headers);

	TWeakPtr<FMRConnection> Weak = AsShared();
	// The socket's events may come from another thread: everything runs on the game thread.
	auto OnGame = [Weak](TFunction<void(FMRConnection&)> Fn)
	{
		auto Run = [Weak, Fn = MoveTemp(Fn)]()
		{
			if (TSharedPtr<FMRConnection> Self = Weak.Pin())
			{
				Fn(*Self);
			}
		};
		if (IsInGameThread())
		{
			Run();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, MoveTemp(Run));
		}
	};
	Socket->OnConnected().AddLambda([OnGame]()
	{
		OnGame([](FMRConnection& C)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: connected"));
			C.State = EState::Login;  // the server speaks first (AP_GETLOGIN)
		});
	});
	Socket->OnConnectionError().AddLambda([OnGame](const FString& Error)
	{
		OnGame([Error](FMRConnection& C)
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: connection error: %s"), *Error);
			C.Fail(TEXT("Couldn't reach the server."));
		});
	});
	Socket->OnClosed().AddLambda([OnGame](int32 Code, const FString& Reason, bool bClean)
	{
		OnGame([Code, Reason](FMRConnection& C)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: closed (%d %s)"), Code, *Reason);
			C.Fail(C.bClosing ? FString() : TEXT("Disconnected from the server."));
		});
	});
	Socket->OnRawMessage().AddLambda([OnGame](const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
	{
		TArray<uint8> Copy(static_cast<const uint8*>(Data), static_cast<int32>(Size));
		OnGame([Copy = MoveTemp(Copy)](FMRConnection& C) { C.OnBytes(Copy.GetData(), Copy.Num()); });
	});
	UE_LOG(LogMeridian, Log, TEXT("MRNet: connecting to %s"), *Url);
	Socket->Connect();
}

void FMRConnection::Close()
{
	if (State == EState::Closed || State == EState::Idle)
	{
		return;
	}
	bClosing = true;
	if (State == EState::Game)
	{
		Send(FMRWriter(MRMsg::BP_REQ_QUIT));
	}
	if (Socket.IsValid())
	{
		Socket->Close();
	}
	Fail(FString());
}

void FMRConnection::Fail(const FString& Error)
{
	if (State == EState::Closed)
	{
		return;
	}
	State = EState::Closed;
	if (Socket.IsValid() && Socket->IsConnected())
	{
		bClosing = true;
		Socket->Close();
	}
	if (OnClosed)
	{
		OnClosed(Error);
	}
}

void FMRConnection::Tick(float DeltaSeconds)
{
	if (State != EState::Game)
	{
		return;
	}
	PingTimer += DeltaSeconds;
	if (PingTimer >= PingSeconds)
	{
		PingTimer = 0.f;
		Send(FMRWriter(MRMsg::BP_PING));
	}
}

// ------------------------------------------------------------------------------ receiving

void FMRConnection::OnBytes(const void* Data, SIZE_T Size)
{
	Decoder.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
	FMRFrame Frame;
	while (State != EState::Closed && Decoder.Next(Frame))
	{
		HandleFrame(Frame);
	}
	if (Decoder.IsBroken())
	{
		// (no resync: blakserv's handshake can't complete, see the class comment)
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: corrupt frame header"));
		Fail(TEXT("Lost the connection to the server (protocol error)."));
	}
}

void FMRConnection::HandleFrame(FMRFrame& Frame)
{
	if (Frame.Body.Num() == 0)
	{
		return;
	}
	// The server scrambles every type byte once the account is logged in; until the first echo
	// ping the token is 0 and doesn't slide, so decoding everything is always right.
	Token.Decode(Frame.Body);
	if (State == EState::Login || State == EState::Connecting)
	{
		HandleLogin(Frame.Body);
		return;
	}
	if (State != EState::Game)
	{
		return;
	}
	if (Frame.Epoch != 0)
	{
		Epoch = Frame.Epoch;  // our messages carry the latest epoch, or the server ignores them
	}
	switch (Frame.Body[0])
	{
	case MRMsg::BP_ECHO_PING:
	{
		FMRReader R(Frame.Body);
		const uint8 TokenByte = R.U8() ^ 0xED;
		const uint32 RedbookId = R.U32();
		TArray<uint8> Redbook = RedbookId && ResourceBytes ? ResourceBytes(RedbookId) : TArray<uint8>();
		if (Redbook.Num() == 0)
		{
			if (RedbookId)
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: redbook resource %u not in this rsb (not the server's rsc0000.rsb?)"), RedbookId);
			}
			const char* Default = FMRServerToken::DefaultRedbook();
			Redbook.Append(reinterpret_cast<const uint8*>(Default), FCStringAnsi::Strlen(Default));
		}
		Token.Rekey(TokenByte, Redbook);
		return;
	}
	case MRMsg::BP_RESYNC:
		// the server couldn't read a frame of ours and now waits for a beacon it can't recognise
		Fail(TEXT("Lost the connection to the server (protocol error)."));
		return;
	case MRMsg::BP_QUIT:
		// out of the game, back at the server's menu: its AP_GETCHOICE comes next (HandleLogin)
		State = EState::Login;
		break;
	default:
		break;
	}
	if (OnMessage)
	{
		OnMessage(Frame.Body);
	}
}

void FMRConnection::HandleLogin(const TArray<uint8>& Body)
{
	FMRReader R(Body);
	switch (Body[0])
	{
	case MRMsg::AP_GETLOGIN:
	{
		State = EState::Login;
		FMRWriter W(MRMsg::AP_LOGIN);
		W.U8(VersionMajor).U8(VersionMinor);
		W.I32(1).I32(10).I32(0);      // OS type, major, minor (synched.c only logs these)
		W.I32(16384).I32(586);        // RAM (MB), CPU
		W.U16(1920).U16(1080);        // screen
		W.I32(1).I32(0).I32(32);      // displays, bandwidth, reserved (low byte: colour depth)
		W.Str(Login.User);
		W.Raw(MRProto::PasswordDigest(Login.Password));
		W.Str(Login.SecretKey);
		SendLogin(W.Bytes);
		return;
	}
	case MRMsg::AP_LOGINOK:
		UE_LOG(LogMeridian, Log, TEXT("MRNet: logged in (account type %d)"), R.U8());
		return;
	case MRMsg::AP_GETCHOICE:
	{
		uint32 Seeds[5];
		for (uint32& S : Seeds)
		{
			S = R.U32();
		}
		Streams.Seed(Seeds);
		FMRWriter W(MRMsg::AP_REQ_GAME);
		W.I32(0).I32(0).Str(FString());  // last download time, catch, host name
		SendLogin(W.Bytes);
		return;
	}
	case MRMsg::AP_CREDITS:
		return;
	case MRMsg::AP_GAME:
		UE_LOG(LogMeridian, Log, TEXT("MRNet: game mode"));
		State = EState::Game;
		PingTimer = 0.f;
		if (OnGameMode)
		{
			OnGameMode();
		}
		return;
	case MRMsg::AP_LOGINFAILED:
		Fail(TEXT("Login failed. Check your password."));
		return;
	case MRMsg::AP_ACCOUNTUSED:
		Fail(TEXT("That account is already in use."));
		return;
	case MRMsg::AP_TOOMANYLOGINS:
		Fail(TEXT("Too many failed logins."));
		return;
	case MRMsg::AP_NOCHARACTERS:
		Fail(TEXT("This account has no character slots."));
		return;
	case MRMsg::AP_GETCLIENT:
	case MRMsg::AP_CLIENT_PATCH:
		Fail(TEXT("The server rejected this client (wrong secret key or version)."));
		return;
	case MRMsg::AP_DOWNLOAD:
		Fail(TEXT("The server wants to send files this client can't download."));
		return;
	case MRMsg::AP_TIMEOUT:
		Fail(TEXT("The server timed out the login."));
		return;
	case MRMsg::AP_MESSAGE:
	{
		const FString Text = R.Str();
		Fail(Text.IsEmpty() ? TEXT("The server refused the login.") : Text);
		return;
	}
	default:
		UE_LOG(LogMeridian, Verbose, TEXT("MRNet: login message %d ignored"), Body[0]);
		return;
	}
}

// ------------------------------------------------------------------------------ sending

void FMRConnection::SendLogin(const TArray<uint8>& Body)
{
	SendRaw(MRProto::EncodeFrame(Body, MRProto::Crc16(Body), 0));
}

void FMRConnection::Send(const TArray<uint8>& Body)
{
	if (State != EState::Game || Body.Num() == 0)
	{
		return;
	}
	// every message steps the security streams, so it must be sent, in this order
	SendRaw(MRProto::EncodeFrame(Body, Streams.Next(Body), Epoch));
}

void FMRConnection::SendRaw(const TArray<uint8>& Frame)
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Send(Frame.GetData(), Frame.Num(), true);
	}
}

#undef LOCTEXT_NAMESPACE
