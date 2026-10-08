#include "Net/MRNetSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRConnection.h"
#include "Net/MRProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRMapCapture.h"
#include "Tests/MRMonsterTour.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRSpriteNetTest.h"
#include "Tests/MRMoveTest.h"
#include "Tests/MRUIShots.h"
#include "Tests/MRZoneSmokeTest.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	const TCHAR* ConfigSection = TEXT("MR.Net");
	constexpr int32 MaxChatLines = 200;
}

bool FMRNetObject::IsPlayer() const
{
	return (Flags & MRMsg::OF_PLAYER) != 0;
}

bool FMRNetObject::IsCreature() const
{
	return (Flags & (MRMsg::OF_PLAYER | MRMsg::OF_ATTACKABLE | MRMsg::OF_NPC)) != 0;
}

// ------------------------------------------------------------------------------ setup

void UMRNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadServers();
	if (GConfig)
	{
		GConfig->GetInt(ConfigSection, TEXT("LastServer"), LastServer, GGameUserSettingsIni);
		GConfig->GetString(ConfigSection, TEXT("LastUser"), LastUser, GGameUserSettingsIni);
	}
	LastServer = Servers.IsValidIndex(LastServer) ? LastServer : 0;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMRNetSubsystem::Tick));
}

void UMRNetSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (Connection.IsValid())
	{
		Connection->OnClosed = nullptr;
		Connection->OnMessage = nullptr;
		Connection->Close();
		Connection.Reset();
	}
	Retired.Reset();
	Super::Deinitialize();
}

bool UMRNetSubsystem::IsOfflineRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MROffline"))
		|| UMRScreenshotTour::IsRequested() || UMRProfileTour::IsRequested() || UMRLookDevTour::IsRequested()
		|| UMRMapCapture::IsRequested() || UMRMonsterTour::IsRequested() || UMRSpriteClipTour::IsRequested()
		|| UMRSpriteNetTest::IsRequested() || UMRUIShots::IsRequested() || UMRZoneSmokeTest::IsRequested()
		|| UMRMoveTest::IsRequested();
}

bool UMRNetSubsystem::WantsOnline(const UWorld* World)
{
	return World && World->IsGameWorld() && World->GetNetMode() == NM_Standalone && !IsOfflineRequested();
}

void UMRNetSubsystem::LoadServers()
{
	Servers.Reset();
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("net"), TEXT("servers.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogMeridian, Error, TEXT("MRNet: no server list at %s"), *Path);
		return;
	}
	const FString DefaultOrigin = Root->GetStringField(TEXT("origin"));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("servers")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		FMRServerEntry E;
		E.Name = S->GetStringField(TEXT("name"));
		E.Ws = S->GetStringField(TEXT("ws"));
		E.Assets = S->GetStringField(TEXT("assets"));
		E.SecretKey = S->GetStringField(TEXT("secret_key"));
		S->TryGetStringField(TEXT("ruleset"), E.Ruleset);
		if (!S->TryGetStringField(TEXT("origin"), E.Origin))
		{
			E.Origin = DefaultOrigin;
		}
		Servers.Add(MoveTemp(E));
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: %d servers in %s"), Servers.Num(), *Path);
}

void UMRNetSubsystem::SetPhase(EMRNetPhase InPhase, const FString& InStatus)
{
	Status = InStatus;
	if (Phase != InPhase)
	{
		Phase = InPhase;
		OnPhaseChanged.Broadcast();
	}
}

bool UMRNetSubsystem::Tick(float DeltaSeconds)
{
	Retired.Reset();
	if (Connection.IsValid())
	{
		Connection->Tick(DeltaSeconds);
	}
	return true;
}

void UMRNetSubsystem::SetPreview(EMRNetPhase InPhase, const TArray<FMRCharacterSlot>& InCharacters, const FString& InError)
{
	Characters = InCharacters;
	LastError = InError;
	bSubmittingCharacter = false;
	SetPhase(InPhase);
	OnCharactersChanged.Broadcast();
}

void UMRNetSubsystem::SetPreviewCharInfo(const FMRCharInfo& Info, uint32 SlotId)
{
	CharInfo = Info;
	CreateSlot = SlotId;
	LastError.Reset();
	bSubmittingCharacter = false;
	SetPhase(EMRNetPhase::Creating);
	OnCharInfo.Broadcast();
}

bool UMRNetSubsystem::LoadMockCharInfo(FMRCharInfo& Out)
{
	FString Json;
	const FString File = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("charinfo.json"));
	if (!FFileHelper::LoadFileToString(Json, *File) || !MRCharInfo::LoadMock(Json, Out))
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: %s missing or unreadable (tools/kod_extract/extract.py)"), *File);
		return false;
	}
	return true;
}

// ------------------------------------------------------------------------------ session

void UMRNetSubsystem::Connect(int32 InServer, const FString& User, const FString& Password)
{
	if (!Servers.IsValidIndex(InServer) || Phase != EMRNetPhase::Offline)
	{
		return;
	}
	ServerIndex = InServer;
	PendingUser = User.TrimStartAndEnd();
	PendingPassword = Password;
	LastError.Reset();
	LastServer = InServer;
	LastUser = PendingUser;
	if (GConfig)
	{
		GConfig->SetInt(ConfigSection, TEXT("LastServer"), LastServer, GGameUserSettingsIni);
		GConfig->SetString(ConfigSection, TEXT("LastUser"), *LastUser, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	SetPhase(EMRNetPhase::Connecting, TEXT("Fetching game data..."));
	FetchGameData();
}

void UMRNetSubsystem::Logoff()
{
	PendingPassword.Reset();
	if (Connection.IsValid())
	{
		Connection->Close();  // HandleClosed resets the state
	}
	else
	{
		SetPhase(EMRNetPhase::Offline);
	}
}

void UMRNetSubsystem::HandleClosed(const FString& Error)
{
	if (Connection.IsValid())
	{
		Retired.Add(Connection);
		Connection.Reset();
	}
	PendingPassword.Reset();
	LastError = Error;
	Objects.Reset();
	Player = FMRNetPlayer();
	Characters.Reset();
	CharInfo = FMRCharInfo();
	bSubmittingCharacter = false;
	StatGroups.Reset();
	bRequestedStats = false;
	Resources.ClearDynamic();
	if (!Error.IsEmpty())
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: %s"), *Error);
	}
	SetPhase(EMRNetPhase::Offline);
}

void UMRNetSubsystem::UseCharacter(uint32 Id)
{
	// (never for a slot still to be created: blakserv hangs up and blocks the address for a while)
	if (Connection.IsValid() && (Phase == EMRNetPhase::Characters || Phase == EMRNetPhase::Creating))
	{
		bSubmittingCharacter = false;
		Connection->Send(FMRWriter(MRMsg::BP_USE_CHARACTER).U32(Id));
		bAwaitingRoom = true;
		SetPhase(EMRNetPhase::Entering, TEXT("Entering Meridian..."));
	}
}

void UMRNetSubsystem::RequestCharInfo(uint32 SlotId)
{
	if (!Connection.IsValid() || Phase != EMRNetPhase::Characters)
	{
		return;
	}
	// BP_SYSTEM + BP_SEND_CHARINFO: the System object answers with BP_CHARINFO (system.kod SendCharInfo)
	CreateSlot = SlotId;
	CharInfo = FMRCharInfo();
	LastError.Reset();
	Connection->Send(FMRWriter(MRMsg::BP_SYSTEM).U8(MRMsg::BP_SEND_CHARINFO));
	SetPhase(EMRNetPhase::Creating, TEXT("Asking the server for its choices..."));
}

void UMRNetSubsystem::CreateCharacter(const FMRNewCharacter& Character)
{
	if (!Connection.IsValid() || Phase != EMRNetPhase::Creating || !CharInfo.IsValid() || bSubmittingCharacter)
	{
		return;
	}
	// BP_SYSTEM + BP_NEW_CHARINFO (blakserv sprocket.c system_def_table, MRCharInfo::Write)
	FMRNewCharacter C = Character;
	C.SlotId = CreateSlot;
	Connection->Send(MRCharInfo::Write(C, CharInfo));
	bSubmittingCharacter = true;
	Status = TEXT("Creating your character...");
	LastError.Reset();
	OnCharactersChanged.Broadcast();
}

void UMRNetSubsystem::CancelCreation()
{
	if (Phase == EMRNetPhase::Creating && !bSubmittingCharacter)
	{
		LastError.Reset();
		SetPhase(EMRNetPhase::Characters);
		OnCharactersChanged.Broadcast();
	}
}

void UMRNetSubsystem::RequestMove(int32 KodRow, int32 KodCol, uint8 Speed)
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_MOVE)
			.U16(static_cast<uint16>(FMath::Clamp(KodRow, 0, 65535)))
			.U16(static_cast<uint16>(FMath::Clamp(KodCol, 0, 65535)))
			.U8(Speed).U32(Player.RoomObjectId));
	}
}

void UMRNetSubsystem::RequestTurn(int32 KodAngle)
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_TURN).U32(Player.Id).U16(static_cast<uint16>(((KodAngle % 4096) + 4096) % 4096)));
	}
}

void UMRNetSubsystem::RequestGo()
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_GO));
	}
}

void UMRNetSubsystem::Say(const FString& Text)
{
	const FString Line = Text.TrimStartAndEnd();
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame && !Line.IsEmpty())
	{
		Connection->Send(FMRWriter(MRMsg::BP_SAY_TO).U8(MRMsg::SAY_NORMAL).Str(Line.Left(250)));
	}
}

// ------------------------------------------------------------------------------ game data

FString UMRNetSubsystem::CacheDir() const
{
	const FMRServerEntry* S = GetServer();
	FString Slug = S ? S->Name : TEXT("server");
	Slug = FPaths::MakeValidFileName(Slug.Replace(TEXT(" "), TEXT("_")));
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MRNet"), Slug);
}

bool UMRNetSubsystem::LoadCachedRsb()
{
	TArray<uint8> Bytes;
	FString Hash;
	const FString Dir = CacheDir();
	if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Dir, TEXT("rsc0000.rsb"))) || !Resources.Load(Bytes))
	{
		return false;
	}
	FFileHelper::LoadFileToString(Hash, *FPaths::Combine(Dir, TEXT("rsc0000.hash")));
	LoadedRsbHash = Hash.TrimStartAndEnd();
	UE_LOG(LogMeridian, Log, TEXT("MRNet: %d resources from %s"), Resources.Num(), *Dir);
	return true;
}

void UMRNetSubsystem::FetchGameData()
{
	const FMRServerEntry* S = GetServer();
	if (!S || S->Assets.IsEmpty())
	{
		if (Resources.IsLoaded() || LoadCachedRsb())
		{
			OpenSocket();
		}
		else
		{
			HandleClosed(TEXT("This server has no game data address."));
		}
		return;
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(S->Assets / TEXT("manifest.json"));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(15.f);
	Req->OnProcessRequestComplete().BindUObject(this, &UMRNetSubsystem::OnManifest);
	Req->ProcessRequest();
}

void UMRNetSubsystem::OnManifest(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk)
{
	if (Phase != EMRNetPhase::Connecting)
	{
		return;  // cancelled
	}
	FString Hash;
	TSharedPtr<FJsonObject> Root;
	if (bOk && Response.IsValid() && Response->GetResponseCode() == 200
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Root) && Root.IsValid())
	{
		Root->TryGetStringField(TEXT("rsbHash"), Hash);
	}
	if (Hash.IsEmpty())
	{
		// no manifest: play with what we have, if anything
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: no manifest from %s"), Request.IsValid() ? *Request->GetURL() : TEXT("?"));
		if ((Resources.IsLoaded() && !LoadedRsbHash.IsEmpty()) || LoadCachedRsb())
		{
			OpenSocket();
		}
		else
		{
			HandleClosed(FString::Printf(TEXT("Couldn't download the game data from %s."), *GetServer()->Assets));
		}
		return;
	}
	if ((Resources.IsLoaded() && LoadedRsbHash == Hash) || (LoadCachedRsb() && LoadedRsbHash == Hash))
	{
		OpenSocket();
		return;
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FString::Printf(TEXT("%s/rsc0000.rsb?v=%s"), *GetServer()->Assets, *Hash));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(60.f);
	Req->OnProcessRequestComplete().BindUObject(this, &UMRNetSubsystem::OnRsb, Hash);
	Req->ProcessRequest();
	Status = TEXT("Downloading game data...");
}

void UMRNetSubsystem::OnRsb(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk, FString Hash)
{
	if (Phase != EMRNetPhase::Connecting)
	{
		return;
	}
	if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200 || !Resources.Load(Response->GetContent()))
	{
		HandleClosed(TEXT("Couldn't download the server's resources (rsc0000.rsb)."));
		return;
	}
	const FString Dir = CacheDir();
	IFileManager::Get().MakeDirectory(*Dir, true);
	FFileHelper::SaveArrayToFile(Response->GetContent(), *FPaths::Combine(Dir, TEXT("rsc0000.rsb")));
	FFileHelper::SaveStringToFile(Hash, *FPaths::Combine(Dir, TEXT("rsc0000.hash")));
	LoadedRsbHash = Hash;
	UE_LOG(LogMeridian, Log, TEXT("MRNet: downloaded %d resources (%s)"), Resources.Num(), *Hash);
	OpenSocket();
}

void UMRNetSubsystem::OpenSocket()
{
	const FMRServerEntry* S = GetServer();
	if (!S || Phase != EMRNetPhase::Connecting)
	{
		return;
	}
	Status = TEXT("Connecting...");
	Connection = MakeShared<FMRConnection>();
	TWeakObjectPtr<UMRNetSubsystem> Weak(this);
	Connection->OnMessage = [Weak](const TArray<uint8>& Body)
	{
		if (UMRNetSubsystem* Self = Weak.Get())
		{
			Self->HandleMessage(Body);
		}
	};
	Connection->OnGameMode = [Weak]()
	{
		if (UMRNetSubsystem* Self = Weak.Get())
		{
			Self->Status = TEXT("Loading characters...");
			Self->OnCharactersChanged.Broadcast();
		}
	};
	Connection->OnClosed = [Weak](const FString& Error)
	{
		if (UMRNetSubsystem* Self = Weak.Get())
		{
			Self->HandleClosed(Error);
		}
	};
	Connection->ResourceBytes = [Weak](uint32 Id)
	{
		const UMRNetSubsystem* Self = Weak.Get();
		return Self ? Self->Resources.Bytes(Id) : TArray<uint8>();
	};
	FMRConnection::FLogin Login;
	Login.User = PendingUser;
	Login.Password = PendingPassword;
	Login.SecretKey = S->SecretKey;
	PendingPassword.Reset();  // only the connection keeps it, for the one login message
	Connection->Connect(S->Ws, S->Origin, Login);
}

// ------------------------------------------------------------------------------ messages

void UMRNetSubsystem::SkipPalette(FMRReader& R)
{
	// an optional prefix: a palette translation or a drawing effect (proto.h ANIMATE_TRANSLATION/EFFECT)
	const uint8 Next = R.Peek();
	if (Next == MRMsg::ANIMATE_TRANSLATION || Next == MRMsg::ANIMATE_EFFECT)
	{
		R.Skip(2);
	}
}

int32 UMRNetSubsystem::ReadPalette(FMRReader& R)
{
	const uint8 Next = R.Peek();
	if (Next == MRMsg::ANIMATE_TRANSLATION)
	{
		R.U8();
		return R.U8();
	}
	if (Next == MRMsg::ANIMATE_EFFECT)
	{
		R.Skip(2);
	}
	return -1;
}

void UMRNetSubsystem::SkipAnimation(FMRReader& R)
{
	switch (R.U8())
	{
	case MRMsg::ANIMATE_NONE: R.Skip(2); break;          // group
	case MRMsg::ANIMATE_CYCLE: R.Skip(4 + 2 + 2); break; // period, low, high
	case MRMsg::ANIMATE_ONCE: R.Skip(4 + 2 + 2 + 2); break;
	default: break;
	}
}

void UMRNetSubsystem::ReadOverlays(FMRReader& R, TArray<FString>* Out, TArray<FMRNetOverlay>* OutParts)
{
	const int32 N = R.U8();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		const uint32 Icon = R.U32();
		const uint8 Hotspot = R.U8();
		const int32 Xlat = ReadPalette(R);
		SkipAnimation(R);
		if (Out)
		{
			Out->Add(Resources.Get(Icon));
		}
		if (OutParts)
		{
			OutParts->Add(FMRNetOverlay{FPaths::GetBaseFilename(Resources.Get(Icon)).ToLower(), Hotspot, Xlat});
		}
	}
}

bool UMRNetSubsystem::ReadObject(FMRReader& R, FMRNetObject& Out)
{
	const uint32 RawId = R.U32();
	Out.Id = MRMsg::PlainId(RawId);
	if (MRMsg::IsNumberId(RawId))
	{
		R.U32();  // amount
	}
	Out.IconRsc = R.U32();
	Out.NameRsc = R.U32();
	Out.Flags = R.U32();
	R.U8();  // drawing effect
	Out.MinimapFlags = R.U32();
	R.U32();  // name colour
	Out.ObjectType = R.U8();
	Out.MoveOn = R.U8();
	if (R.U16() != 0)  // light: flags, then intensity and colour
	{
		R.Skip(3);
	}
	Out.Xlat = ReadPalette(R);
	SkipAnimation(R);
	Out.Overlays.Reset();
	Out.OverlayParts.Reset();
	ReadOverlays(R, &Out.Overlays, &Out.OverlayParts);
	Out.Icon = Resources.Get(Out.IconRsc);
	Out.Name = Resources.Get(Out.NameRsc);
	return R.IsOk();
}

bool UMRNetSubsystem::ReadRoomObject(FMRReader& R, FMRNetObject& Out)
{
	if (!ReadObject(R, Out))
	{
		return false;
	}
	Out.KodRow = R.U16();
	Out.KodCol = R.U16();
	Out.Angle = R.U16();
	// the motion state (what it shows while moving): ignored for now
	SkipPalette(R);
	SkipAnimation(R);
	ReadOverlays(R, nullptr);
	return R.IsOk();
}

void UMRNetSubsystem::AddChat(const FString& Text, uint8 Kind)
{
	FMRChatLine Line;
	Line.Text = MRServerText::StripStyle(Text);
	Line.Kind = Kind;
	Line.Time = FPlatformTime::Seconds();
	UE_LOG(LogMeridian, Log, TEXT("MRNet chat: %s"), *Line.Text);
	Chat.Add(Line);
	if (Chat.Num() > MaxChatLines)
	{
		Chat.RemoveAt(0, Chat.Num() - MaxChatLines);
	}
	OnChat.Broadcast(Line);
}

bool UMRNetSubsystem::ReadStat(FMRReader& R, FMRNetStat& Out)
{
	// merintr.c ExtractStatistic: num, name, type, then a numeric value (with limits when it's an
	// integer) or a list entry (object, value, icon)
	Out.Num = R.U8();
	Out.NameRsc = R.U32();
	Out.Name = Resources.Get(Out.NameRsc);
	Out.Type = R.U8();
	if (Out.Type == FMRNetStat::Numeric)
	{
		Out.Tag = R.U8();
		const uint32 Value = R.U32();
		if (Out.Tag == 1)
		{
			Out.Value = static_cast<int32>(Value);
			Out.Min = R.I32();
			Out.Max = R.I32();
			Out.CurrentMax = R.I32();
		}
		else
		{
			Out.ValueText = Resources.Get(Value);
		}
	}
	else if (Out.Type == FMRNetStat::List)
	{
		Out.ObjectId = MRMsg::PlainId(R.U32());
		Out.Value = R.I32();
		Out.IconRsc = R.U32();
		Out.Icon = Resources.Get(Out.IconRsc);
	}
	else
	{
		return false;  // unknown type: the rest of the message can't be read
	}
	return R.IsOk();
}

FMRNetStatGroup& UMRNetSubsystem::StatGroup(uint8 Group)
{
	if (FMRNetStatGroup* G = StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; }))
	{
		return *G;
	}
	FMRNetStatGroup& G = StatGroups.AddDefaulted_GetRef();
	G.Group = Group;
	StatGroups.Sort([](const FMRNetStatGroup& A, const FMRNetStatGroup& B) { return A.Group < B.Group; });
	return *StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; });
}

const FMRNetStatGroup* UMRNetSubsystem::FindStatGroup(uint8 Group) const
{
	return StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; });
}

void UMRNetSubsystem::HandleMessage(const TArray<uint8>& Body)
{
	FMRReader R(Body);
	switch (Body[0])
	{
	case MRMsg::BP_LOAD_MODULE:
	{
		const FString Module = Resources.Get(R.U32());
		UE_LOG(LogMeridian, Log, TEXT("MRNet: module %s"), *Module);
		// the character module (char.dll) asks for the account's characters
		if (Module.StartsWith(TEXT("char"), ESearchCase::IgnoreCase))
		{
			Connection->Send(FMRWriter(MRMsg::BP_SEND_CHARACTERS));
		}
		break;
	}
	case MRMsg::BP_CHARACTERS:
	{
		Characters.Reset();
		const int32 N = R.U16();
		for (int32 i = 0; i < N && R.IsOk(); ++i)
		{
			FMRCharacterSlot Slot;
			Slot.Id = R.U32();
			Slot.Name = R.Str();
			Slot.bNeedsCreation = R.U8() == 1;  // blakserv game.c: 1 = never been in the game
			Characters.Add(Slot);
		}
		Motd = MRServerText::StripStyle(R.Str());
		UE_LOG(LogMeridian, Log, TEXT("MRNet: %d character slots"), Characters.Num());
		SetPhase(EMRNetPhase::Characters);
		OnCharactersChanged.Broadcast();
		break;
	}
	case MRMsg::BP_CHARINFO:
		// the creator's options (MRCharInfo::Parse); only while we asked for them
		if (Phase == EMRNetPhase::Creating)
		{
			FMRCharInfo Info;
			if (MRCharInfo::Parse(R, Resources, Info) && Info.IsValid())
			{
				CharInfo = MoveTemp(Info);
				UE_LOG(LogMeridian, Log, TEXT("MRNet: character options: %d hair, %d male eyes, %d spells, %d skills"),
					CharInfo.Male.Hair.Num(), CharInfo.Male.Eyes.Num(), CharInfo.Spells.Num(), CharInfo.Skills.Num());
				Status.Reset();
				OnCharInfo.Broadcast();
			}
			else
			{
				LastError = TEXT("The server's character choices couldn't be read.");
				SetPhase(EMRNetPhase::Characters);
				OnCharactersChanged.Broadcast();
			}
		}
		break;
	case MRMsg::BP_CHARINFO_OK:
		// the server may have given the character a new object id: enter with that one (charmake.c)
		UE_LOG(LogMeridian, Log, TEXT("MRNet: character created"));
		Status.Reset();
		UseCharacter(R.U32());
		break;
	case MRMsg::BP_CHARINFO_NOT_OK:
		// the server doesn't say why; the original client always blamed the name (charmake.c)
		Status.Reset();
		bSubmittingCharacter = false;
		LastError = TEXT("That name can't be used. Try another.");
		OnCreateFailed.Broadcast();
		OnCharactersChanged.Broadcast();
		break;
	case MRMsg::BP_PLAYER:
	{
		Player.Id = MRMsg::PlainId(R.U32());
		R.U32();  // icon
		R.U32();  // name
		Player.RoomObjectId = R.U32();
		Player.RoomFile = Resources.Get(R.U32());
		Player.RoomName = Resources.Get(R.U32());
		Player.RoomSecurity = R.U32();
		bAwaitingRoom = true;
		UE_LOG(LogMeridian, Log, TEXT("MRNet: room %s (%s)"), *Player.RoomFile, *Player.RoomName);
		break;
	}
	case MRMsg::BP_ROOM_CONTENTS:
	{
		R.U32();  // room object
		const int32 N = R.U16();
		Objects.Reset();
		for (int32 i = 0; i < N; ++i)
		{
			FMRNetObject O;
			if (!ReadRoomObject(R, O))
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: room contents cut short at object %d of %d"), i, N);
				break;
			}
			Objects.Add(O.Id, MoveTemp(O));
		}
		bAwaitingRoom = false;
		SetPhase(EMRNetPhase::InGame);
		OnRoomEntered.Broadcast();
		if (!bRequestedStats)
		{
			// the stat groups' names first, then every group (merintr stats.c StatsGroupsInfo)
			bRequestedStats = true;
			Connection->Send(FMRWriter(MRMsg::BP_SEND_STAT_GROUPS));
		}
		break;
	}
	case MRMsg::BP_STAT_GROUPS:
	{
		const int32 N = R.U8();
		for (int32 i = 1; i <= N && R.IsOk(); ++i)
		{
			FMRNetStatGroup& G = StatGroup(static_cast<uint8>(i));
			G.NameRsc = R.U32();
			G.Name = Resources.Get(G.NameRsc);
		}
		UE_LOG(LogMeridian, Log, TEXT("MRNet: %d stat groups"), N);
		for (int32 i = 1; i <= N; ++i)
		{
			Connection->Send(FMRWriter(MRMsg::BP_SEND_STATS).U8(static_cast<uint8>(i)));
		}
		break;
	}
	case MRMsg::BP_STAT_GROUP:
	{
		const uint8 Group = R.U8();
		const int32 N = R.U8();
		TArray<FMRNetStat> Stats;
		for (int32 i = 0; i < N; ++i)
		{
			FMRNetStat S;
			if (!ReadStat(R, S))
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: stat group %d cut short at %d of %d"), Group, i, N);
				break;
			}
			Stats.Add(MoveTemp(S));
		}
		FMRNetStatGroup& G = StatGroup(Group);
		G.Stats = MoveTemp(Stats);
		G.bReceived = true;
		UE_LOG(LogMeridian, Log, TEXT("MRNet: stat group %d (%s): %d stats"), Group, *G.Name, G.Stats.Num());
		OnStatsChanged.Broadcast(Group);
		break;
	}
	case MRMsg::BP_STAT:
	{
		const uint8 Group = R.U8();
		FMRNetStat S;
		if (ReadStat(R, S))
		{
			FMRNetStatGroup& G = StatGroup(Group);
			if (FMRNetStat* Existing = G.Stats.FindByPredicate([&S](const FMRNetStat& E) { return E.Num == S.Num; }))
			{
				*Existing = MoveTemp(S);
			}
			else
			{
				G.Stats.Add(MoveTemp(S));  // the group's order is the server's (Num isn't the order)
			}
			OnStatsChanged.Broadcast(Group);
		}
		break;
	}
	case MRMsg::BP_CREATE:
	{
		FMRNetObject O;
		if (ReadRoomObject(R, O))
		{
			const uint32 Id = O.Id;
			Objects.Add(Id, MoveTemp(O));
			OnObjectAdded.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_REMOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		if (Objects.Remove(Id) > 0)
		{
			OnObjectRemoved.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_CHANGE:
	{
		FMRNetObject O;
		if (ReadObject(R, O))
		{
			if (FMRNetObject* Existing = Objects.Find(O.Id))
			{
				O.KodRow = Existing->KodRow;
				O.KodCol = Existing->KodCol;
				O.Angle = Existing->Angle;
				O.Speed = Existing->Speed;
				*Existing = MoveTemp(O);
				OnObjectChanged.Broadcast(Existing->Id);
			}
		}
		break;
	}
	case MRMsg::BP_MOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		const int32 Row = R.U16();
		const int32 Col = R.U16();
		const uint8 Speed = R.U8() & 0x7F;  // bit 7: turn to face the way it moves
		if (FMRNetObject* O = Objects.Find(Id); O && R.IsOk())
		{
			O->KodRow = Row;
			O->KodCol = Col;
			O->Speed = Speed;
			OnObjectMoved.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_TURN:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		const int32 Angle = R.U16();
		if (FMRNetObject* O = Objects.Find(Id); O && R.IsOk())
		{
			O->Angle = Angle;
			OnObjectMoved.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_CHANGE_RESOURCE:
	{
		const uint32 Id = R.U32();
		const FString Text = R.Str();
		if (R.IsOk())
		{
			Resources.SetDynamic(Id, Text);
		}
		break;
	}
	case MRMsg::BP_PLAYERS:
	{
		const int32 N = R.U16();
		for (int32 i = 0; i < N && R.IsOk(); ++i)
		{
			R.U32();
			const uint32 NameRsc = R.U32();
			Resources.SetDynamic(NameRsc, R.Str());
			R.Skip(4 + 1 + 4 + 4 + 1 + 1);  // flags, draw type, minimap flags, name colour, object type, move-on
		}
		break;
	}
	case MRMsg::BP_PLAYER_ADD:
	{
		R.U32();
		const uint32 NameRsc = R.U32();
		Resources.SetDynamic(NameRsc, R.Str());
		break;
	}
	case MRMsg::BP_SAID:
	{
		R.U32();  // sender
		R.U32();  // sender's name
		const uint8 Kind = R.U8();
		const uint32 FormatId = R.U32();
		FString Text;
		if (MRServerText::Format(Resources, FormatId, R, Text))
		{
			AddChat(Text, Kind);
		}
		break;
	}
	case MRMsg::BP_MESSAGE:
	case MRMsg::BP_SYS_MESSAGE:
	{
		const uint32 FormatId = R.U32();
		FString Text;
		if (MRServerText::Format(Resources, FormatId, R, Text))
		{
			AddChat(Text, 0);
		}
		break;
	}
	case MRMsg::BP_QUIT:
		Logoff();
		break;
	default:
		break;  // not handled yet (stats, inventory, sounds, lighting...)
	}
}
