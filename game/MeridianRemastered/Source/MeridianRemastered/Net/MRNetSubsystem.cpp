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
#include "Net/MRAssetCache.h"
#include "Net/MRConnection.h"
#include "Net/MRChatCommands.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Net/MRProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRMapCapture.h"
#include "Tests/MRMonsterTour.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRMoveTest.h"
#include "Tests/MRUIShots.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	const TCHAR* ConfigSection = TEXT("MR.Net");
	constexpr int32 MaxChatLines = 200;

	FAutoConsoleCommandWithWorldAndArgs CmdEffect(TEXT("MREffect"),
		TEXT("MREffect <effect> [ms] [xlat]: a server screen effect, as if sent (BP_EFFECT; proto.h EFFECT_*: 1 invert, 2 shake, ")
		TEXT("3 paralyze, 4 release, 5 blind, 6 see, 7 pain, 8 blur, 9 rain, 10 snow, 11 clear, 12 sand, 13 clear sand, 14 waver, ")
		TEXT("15 flash, 16 whiteout, 17 override)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMRNetSubsystem* Net = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
			if (Net && Args.Num() >= 1)
			{
				Net->DebugEffect(static_cast<uint16>(FCString::Atoi(*Args[0])), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 2000,
					Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 0x45);
			}
		}));
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
	Assets = MakeShared<FMRAssetCache>();
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
	if (Assets.IsValid())
	{
		Assets->CancelAll();
	}
	Super::Deinitialize();
}

bool UMRNetSubsystem::IsOfflineRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MROffline"))
		|| UMRScreenshotTour::IsRequested() || UMRProfileTour::IsRequested() || UMRLookDevTour::IsRequested()
		|| UMRMapCapture::IsRequested() || UMRMonsterTour::IsRequested() || UMRSpriteClipTour::IsRequested()
		|| UMRUIShots::IsRequested()
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
	World.Effects.Tick(DeltaSeconds * 1000.f);  // as the original's AnimateEffects
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
	LoginDigest = MRProto::PasswordDigest(Password);
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
	Assets->Configure(Servers[InServer].Assets, CacheDir());
	FetchGameData();
}

void UMRNetSubsystem::ReturnToCharacters()
{
	if (!Connection.IsValid() || Phase != EMRNetPhase::InGame)
	{
		return;
	}
	// game.c: BP_REQ_QUIT logs the character off and answers BP_QUIT (HandleMessage)
	Connection->Send(FMRWriter(MRMsg::BP_REQ_QUIT));
	SetPhase(EMRNetPhase::Connecting, TEXT("Leaving the game..."));
}

void UMRNetSubsystem::Logoff()
{
	PendingPassword.Reset();
	LoginDigest.Reset();
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
	World.Reset();
	Characters.Reset();
	CharInfo = FMRCharInfo();
	bSubmittingCharacter = false;
	bRequestedStats = false;
	bAwaitingRoom = false;
	bWaiting = false;
	if (Assets.IsValid())
	{
		Assets->CancelAll();
	}
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
			.U8(Speed).U32(World.Player.RoomObjectId));
	}
}

void UMRNetSubsystem::RequestTurn(int32 KodAngle)
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_TURN).U32(World.Player.Id).U16(static_cast<uint16>(((KodAngle % 4096) + 4096) % 4096)));
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
	SayAs(MRMsg::SAY_NORMAL, Text);
}

void UMRNetSubsystem::SayAs(uint8 Kind, const FString& Text)
{
	const FString Line = MRChat::FilterSay(Text);
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame && !Line.IsEmpty())
	{
		Connection->Send(FMRWriter(MRMsg::BP_SAY_TO).U8(Kind).Str(Line));
	}
}

void UMRNetSubsystem::SayTo(const TArray<uint32>& Ids, const FString& Text)
{
	// protocol.c PARAM_ID_LIST: u16 count, the ids; then the text
	const FString Line = MRChat::FilterSay(Text);
	if (!Connection.IsValid() || Phase != EMRNetPhase::InGame || Line.IsEmpty() || Ids.IsEmpty())
	{
		return;
	}
	FMRWriter W(MRMsg::BP_SAY_GROUP);
	W.U16(static_cast<uint16>(Ids.Num()));
	for (const uint32 Id : Ids)
	{
		W.U32(Id);
	}
	Connection->Send(W.Str(Line));
}

void UMRNetSubsystem::DoAction(uint8 Action)
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_ACTION).U8(Action));
	}
}

void UMRNetSubsystem::Appeal(const FString& Text)
{
	const FString Line = MRChat::FilterSay(Text);
	if (CanSend() && !Line.IsEmpty())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_APPEAL).Str(Line));
	}
}

void UMRNetSubsystem::RequestTime()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_REQ_TIME));
	}
}

void UMRNetSubsystem::RequestPlayers()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_SEND_PLAYERS));
	}
}

void UMRNetSubsystem::RequestPreferences()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_REQ_PREFERENCES));
	}
}

void UMRNetSubsystem::SetPreferences(uint32 Flags)
{
	if (CanSend())
	{
		World.Preferences = Flags;
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_SEND_PREFERENCES).U32(Flags));
		OnPreferencesChanged.Broadcast();
	}
}

// ------------------------------------------------------------------------------ the others: ignore, groups, aliases

void FMRSocial::DefaultQuickChat()
{
	// module/merintr alias.c: F1 help ... F9 point, F10 addgroup (ours: the menu), F11 mail, F12 quit (ours: who)
	const TCHAR* Lines[12] = {TEXT("help"), TEXT("rest"), TEXT("stand"), TEXT("neutral"), TEXT("happy"), TEXT("sad"), TEXT("wry"),
		TEXT("wave"), TEXT("point"), TEXT(""), TEXT("mail"), TEXT("who")};
	for (int32 i = 0; i < 12; ++i)
	{
		QuickChat[i] = Lines[i];
		QuickRun[i] = true;
	}
}

FString UMRNetSubsystem::CharacterFile(const TCHAR* What) const
{
	return FPaths::Combine(CacheDir(), What, FPaths::MakeValidFileName(LoadedCharacter) + TEXT(".json"));
}

namespace
{
	TSharedRef<FJsonObject> StringsToJson(const TMap<FString, TArray<FString>>& Map)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, TArray<FString>>& P : Map)
		{
			TArray<TSharedPtr<FJsonValue>> A;
			for (const FString& S : P.Value)
			{
				A.Add(MakeShared<FJsonValueString>(S));
			}
			O->SetArrayField(P.Key, A);
		}
		return O;
	}

	bool WriteJson(const TSharedRef<FJsonObject>& Root, const FString& Path)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, W);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}
}

void UMRNetSubsystem::LoadCharacterData()
{
	const FString Name = Resources.Get(World.Player.NameRsc);
	if (Name.IsEmpty() || Name == LoadedCharacter)
	{
		return;
	}
	LoadedCharacter = Name;
	Social = FMRSocial();
	Social.DefaultQuickChat();
	if (const TSharedPtr<FJsonObject> Root = ReadJson(CharacterFile(TEXT("social"))))
	{
		Root->TryGetStringArrayField(TEXT("ignored"), Social.Ignored);
		Root->TryGetBoolField(TEXT("ignore_all"), Social.bIgnoreAll);
		Root->TryGetBoolField(TEXT("no_broadcast"), Social.bNoBroadcast);
		Root->TryGetBoolField(TEXT("timestamps"), Social.bTimestamps);
		const TSharedPtr<FJsonObject>* Groups = nullptr;
		if (Root->TryGetObjectField(TEXT("groups"), Groups))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Groups)->Values)
			{
				TArray<FString>& Members = Social.Groups.Add(P.Key);
				for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray())
				{
					Members.Add(V->AsString());
				}
			}
		}
		const TSharedPtr<FJsonObject>* Aliases = nullptr;
		if (Root->TryGetObjectField(TEXT("aliases"), Aliases))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Aliases)->Values)
			{
				Social.Aliases.Add(P.Key, P.Value->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Quick = nullptr;
		if (Root->TryGetArrayField(TEXT("quick_chat"), Quick))
		{
			for (int32 i = 0; i < Quick->Num() && i < 12; ++i)
			{
				if (const TSharedPtr<FJsonObject> Q = (*Quick)[i]->AsObject())
				{
					Social.QuickChat[i] = Q->GetStringField(TEXT("text"));
					Social.QuickRun[i] = Q->GetBoolField(TEXT("run"));
				}
			}
		}
		Root->TryGetStringArrayField(TEXT("hotbar"), Social.Hotbar);
		Root->TryGetStringArrayField(TEXT("spell_bar"), Social.SpellBar);
		const TSharedPtr<FJsonObject>* Notes = nullptr;
		if (Root->TryGetObjectField(TEXT("map_notes"), Notes))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Notes)->Values)
			{
				TArray<FMRMapNote>& List = Social.MapNotes.Add(P.Key);
				for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray())
				{
					const TSharedPtr<FJsonObject> O = V->AsObject();
					List.Add({O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y")), O->GetStringField(TEXT("text"))});
				}
			}
		}
	}
	Mailbox.Reset();
	if (const TSharedPtr<FJsonObject> Root = ReadJson(CharacterFile(TEXT("mail"))))
	{
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("mail")))
		{
			const TSharedPtr<FJsonObject> M = V->AsObject();
			FMRNetMail& Mail = Mailbox.AddDefaulted_GetRef();
			Mail.From = M->GetStringField(TEXT("from"));
			M->TryGetStringArrayField(TEXT("to"), Mail.To);
			Mail.Time = static_cast<int64>(M->GetNumberField(TEXT("time")));
			Mail.Subject = M->GetStringField(TEXT("subject"));
			Mail.Body = M->GetStringField(TEXT("body"));
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: %s: %d ignored, %d groups, %d aliases, %d kept mail"), *Name, Social.Ignored.Num(),
		Social.Groups.Num(), Social.Aliases.Num(), Mailbox.Num());
	OnMailChanged.Broadcast();
	OnCharacterDataLoaded.Broadcast();
}

void UMRNetSubsystem::SaveSocial() const
{
	if (LoadedCharacter.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Ignored;
	for (const FString& S : Social.Ignored)
	{
		Ignored.Add(MakeShared<FJsonValueString>(S));
	}
	Root->SetArrayField(TEXT("ignored"), Ignored);
	Root->SetBoolField(TEXT("ignore_all"), Social.bIgnoreAll);
	Root->SetBoolField(TEXT("no_broadcast"), Social.bNoBroadcast);
	Root->SetBoolField(TEXT("timestamps"), Social.bTimestamps);
	Root->SetObjectField(TEXT("groups"), StringsToJson(Social.Groups));
	TSharedRef<FJsonObject> Aliases = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& A : Social.Aliases)
	{
		Aliases->SetStringField(A.Key, A.Value);
	}
	Root->SetObjectField(TEXT("aliases"), Aliases);
	TArray<TSharedPtr<FJsonValue>> Quick;
	for (int32 i = 0; i < 12; ++i)
	{
		TSharedRef<FJsonObject> Q = MakeShared<FJsonObject>();
		Q->SetStringField(TEXT("text"), Social.QuickChat[i]);
		Q->SetBoolField(TEXT("run"), Social.QuickRun[i]);
		Quick.Add(MakeShared<FJsonValueObject>(Q));
	}
	Root->SetArrayField(TEXT("quick_chat"), Quick);
	auto Strings = [](const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In)
		{
			Out.Add(MakeShared<FJsonValueString>(S));
		}
		return Out;
	};
	Root->SetArrayField(TEXT("hotbar"), Strings(Social.Hotbar));
	Root->SetArrayField(TEXT("spell_bar"), Strings(Social.SpellBar));
	TSharedRef<FJsonObject> Notes = MakeShared<FJsonObject>();
	for (const TPair<FString, TArray<FMRMapNote>>& P : Social.MapNotes)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FMRMapNote& N : P.Value)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), N.X);
			O->SetNumberField(TEXT("y"), N.Y);
			O->SetStringField(TEXT("text"), N.Text);
			List.Add(MakeShared<FJsonValueObject>(O));
		}
		if (List.Num() > 0)
		{
			Notes->SetArrayField(P.Key, List);
		}
	}
	Root->SetObjectField(TEXT("map_notes"), Notes);
	WriteJson(Root, CharacterFile(TEXT("social")));
}

bool UMRNetSubsystem::IsIgnored(const FString& Name) const
{
	return Social.Ignored.ContainsByPredicate([&Name](const FString& S) { return S.Equals(Name, ESearchCase::IgnoreCase); });
}

void UMRNetSubsystem::SetIgnored(const FString& Name, bool bIgnore)
{
	Social.Ignored.RemoveAll([&Name](const FString& S) { return S.Equals(Name, ESearchCase::IgnoreCase); });
	if (bIgnore && !Name.IsEmpty())
	{
		Social.Ignored.Add(Name);
	}
	SaveSocial();
	OnUsersChanged.Broadcast();
}

TArray<TPair<uint32, FString>> UMRNetSubsystem::GetUserNames() const
{
	TArray<TPair<uint32, FString>> Out;
	for (const TPair<uint32, FMRNetUser>& U : World.Users)
	{
		Out.Add({U.Key, U.Value.Name});
	}
	return Out;
}

// ------------------------------------------------------------------------------ the account

FString UMRNetSubsystem::ChangePassword(const FString& Old, const FString& New)
{
	// clientd3d maindlg.c: at least 6 characters; both sent hashed, as at login (blakserv game.c
	// compares the old with the account's and keeps the new as it comes)
	if (New.Len() < 6)
	{
		return TEXT("The new password needs at least 6 characters.");
	}
	if (New.Len() > 30)
	{
		return TEXT("The new password can have at most 30 characters.");
	}
	if (!Connection.IsValid() || Phase != EMRNetPhase::InGame)
	{
		return TEXT("Not now.");
	}
	FMRWriter W(MRMsg::BP_CHANGE_PASSWORD);
	for (const FString* P : {&Old, &New})
	{
		W.Raw(MRProto::PasswordDigest(*P));  // (Raw: u16 length, then the bytes: a string, as blakserv reads it)
	}
	Connection->Send(W);
	UE_LOG(LogMeridian, Log, TEXT("MRNet: asked to change the password (%d bytes)"), W.Bytes.Num());
	return FString();
}

bool UMRNetSubsystem::IsLoginPassword(const FString& Password) const
{
	return LoginDigest.Num() > 0 && MRProto::PasswordDigest(Password) == LoginDigest;
}

void UMRNetSubsystem::DeleteCharacter()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_SUICIDE));
	}
}

// ------------------------------------------------------------------------------ mail

void UMRNetSubsystem::SaveMail() const
{
	if (LoadedCharacter.IsEmpty())
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FMRNetMail& M : Mailbox)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("from"), M.From);
		TArray<TSharedPtr<FJsonValue>> To;
		for (const FString& T : M.To)
		{
			To.Add(MakeShared<FJsonValueString>(T));
		}
		O->SetArrayField(TEXT("to"), To);
		O->SetNumberField(TEXT("time"), static_cast<double>(M.Time));
		O->SetStringField(TEXT("subject"), M.Subject);
		O->SetStringField(TEXT("body"), M.Body);
		List.Add(MakeShared<FJsonValueObject>(O));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("mail"), List);
	WriteJson(Root, CharacterFile(TEXT("mail")));
}

void UMRNetSubsystem::RequestMail()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_GET_MAIL));
	}
}

void UMRNetSubsystem::DeleteMail(int32 Index)
{
	if (Mailbox.IsValidIndex(Index))
	{
		Mailbox.RemoveAt(Index);
		SaveMail();
		OnMailChanged.Broadcast();
	}
}

void UMRNetSubsystem::SendMail(const TArray<FString>& To, const FString& Subject, const FString& Body)
{
	// mailsend.c: the names (no repeats) go to the server to look up; the answer sends the mail
	TArray<FString> Names;
	for (const FString& N : To)
	{
		const FString T = N.TrimStartAndEnd();
		if (!T.IsEmpty() && !Names.ContainsByPredicate([&T](const FString& S) { return S.Equals(T, ESearchCase::IgnoreCase); }))
		{
			Names.Add(T);
		}
	}
	if (!CanSend() || Names.IsEmpty() || Names.Num() > 20 || IsSendingMail())
	{
		OnMailSent.Broadcast(false, Names.IsEmpty() ? TEXT("Say who it's to.") : Names.Num() > 20 ? TEXT("At most 20 people.") : TEXT("Not now."));
		return;
	}
	PendingMail.To = Names;
	PendingMail.Text = TEXT("Subject: ") + Subject.Replace(TEXT("\n"), TEXT(" ")).Left(50) + TEXT("\n") + Body;
	Connection->Send(FMRWriter(MRMsg::BP_REQ_LOOKUP_NAMES).U16(static_cast<uint16>(Names.Num())).Str(FString::Join(Names, TEXT(","))));
}

// ------------------------------------------------------------------------------ news

void UMRNetSubsystem::RequestArticles()
{
	if (CanSend() && World.News.bOpen)
	{
		World.News.Articles.Reset();
		World.News.bHaveArticles = false;
		Connection->Send(FMRWriter(MRMsg::BP_REQ_ARTICLES).U16(World.News.Group));
	}
}

void UMRNetSubsystem::ReadArticle(uint32 Num)
{
	if (CanSend() && World.News.bOpen)
	{
		World.News.ReadingNum = Num;
		World.News.bHaveText = false;
		World.News.ReadingText.Reset();
		Connection->Send(FMRWriter(MRMsg::BP_REQ_ARTICLE).U16(World.News.Group).U32(Num));
	}
}

void UMRNetSubsystem::PostArticle(const FString& Title, const FString& Body)
{
	if (CanSend() && World.News.bOpen && !Title.TrimStartAndEnd().IsEmpty())
	{
		Connection->Send(FMRWriter(MRMsg::BP_POST_ARTICLE).U16(World.News.Group).Str(Title.Left(50)).Str(Body));
	}
}

void UMRNetSubsystem::DeleteArticle(uint32 Num)
{
	if (CanSend() && World.News.bOpen)
	{
		Connection->Send(FMRWriter(MRMsg::BP_DELETE_NEWS).U16(World.News.Group).U32(Num));
	}
}

void UMRNetSubsystem::CloseNews()
{
	World.News = FMRNetNews();
	OnNewsChanged.Broadcast();
}

// ------------------------------------------------------------------------------ guilds

void UMRNetSubsystem::GuildCommand(uint8 Command, uint32 Id)
{
	if (!CanSend())
	{
		return;
	}
	FMRWriter W(MRMsg::BP_USERCOMMAND);
	W.U8(Command);
	switch (Command)
	{
	case MRMsg::UC_INVITE: case MRMsg::UC_EXILE: case MRMsg::UC_ABDICATE: case MRMsg::UC_VOTE:
	case MRMsg::UC_MAKE_ALLIANCE: case MRMsg::UC_END_ALLIANCE: case MRMsg::UC_MAKE_ENEMY: case MRMsg::UC_END_ENEMY:
		W.U32(Id);
		break;
	default:
		break;
	}
	Connection->Send(W);
}

void UMRNetSubsystem::GuildSetRank(uint32 Id, uint8 Rank)
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_SET_RANK).U32(Id).U8(Rank));
	}
}

void UMRNetSubsystem::GuildCreate(const FString& Name, const TArray<FString>& Ranks, bool bSecret)
{
	// merintr.c user_msg_table: the name, ten rank names (each rank's male then female), u8 secret
	if (!CanSend() || Ranks.Num() != 2 * MRMsg::GuildRanks)
	{
		return;
	}
	FMRWriter W(MRMsg::BP_USERCOMMAND);
	W.U8(MRMsg::UC_GUILD_CREATE).Str(Name.Left(30));
	for (const FString& R : Ranks)
	{
		W.Str(R.Left(20));
	}
	Connection->Send(W.U8(bSecret ? 1 : 0));
}

void UMRNetSubsystem::GuildSetPassword(const FString& Password)
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_GUILD_SET_PASSWORD).Str(Password.Left(30)));
	}
}

void UMRNetSubsystem::RequestLook(uint32 ObjectId)
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame && ObjectId && !IsWaiting())
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_LOOK).U32(ObjectId));
	}
}

bool UMRNetSubsystem::CanSend() const
{
	return Connection.IsValid() && Phase == EMRNetPhase::InGame && !IsWaiting();
}

void UMRNetSubsystem::WriteItem(FMRWriter& W, uint32 ItemId, uint32 Amount) const
{
	// a number item goes with CLIENT_TAG_NUMBER and an amount (parsecli.c reads one after such an id)
	const FMRNetObject* O = World.FindInventory(ItemId);
	if (!O && World.ContentsOf)
	{
		O = World.Contents.FindByPredicate([ItemId](const FMRNetObject& C) { return C.Id == ItemId; });
	}
	if (!O)
	{
		O = World.Objects.Find(ItemId);
	}
	if (!O)
	{
		// something a shop sells (a number item: as many as asked for, not the shop's stack)
		if (const FMRNetForSale* S = World.Shop.Items.FindByPredicate([ItemId](const FMRNetForSale& E) { return E.Object.Id == ItemId; }))
		{
			if (S->Object.bNumber)
			{
				W.U32(MRMsg::NumberId(ItemId)).U32(FMath::Max(1u, Amount));
				return;
			}
		}
	}
	if (O && O->bNumber)
	{
		W.U32(MRMsg::NumberId(ItemId)).U32(Amount > 0 ? FMath::Min(Amount, O->Amount) : O->Amount);
	}
	else
	{
		W.U32(ItemId);
	}
}

void UMRNetSubsystem::RequestInventory()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_INVENTORY));
	}
}

void UMRNetSubsystem::Attack(uint32 TargetId)
{
	if (CanSend() && TargetId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_ATTACK).U8(MRMsg::ATTACK_NORMAL).U32(TargetId));
	}
}

const TMap<uint32, int32>& UMRNetSubsystem::HitFormats()
{
	if (HitFormatsRsb != LoadedRsbHash || (HitFormatIds.Num() == 0 && Resources.IsLoaded()))
	{
		// battler.kod's resources, as Kod writes them (the rsb's ids depend on its build)
		static const TCHAR* Texts[] = {
			TEXT("%sYour %s %s %s%q for ~k~B%i~B%s damage."),
			TEXT("%sYour %s %s %s%s for ~k~B%i~B%s damage."),
			TEXT("%s%s%q's %s %s you for ~r~B%i~B%s damage."),
			TEXT("%s%s%s's %s %s you for ~r~B%i~B%s damage."),
		};
		HitFormatIds.Reset();
		for (int32 Kind = 1; Kind <= 4; ++Kind)
		{
			for (const uint32 Id : Resources.FindByText(Texts[Kind - 1]))
			{
				HitFormatIds.Add(Id, Kind);
			}
		}
		HitFormatsRsb = LoadedRsbHash;
	}
	return HitFormatIds;
}

void UMRNetSubsystem::WriteItems(FMRWriter& W, const TArray<FItemCount>& Items) const
{
	W.U16(static_cast<uint16>(Items.Num()));
	for (const FItemCount& I : Items)
	{
		WriteItem(W, I.Id, I.Amount);
	}
}

void UMRNetSubsystem::RequestBuy(uint32 SellerId)
{
	if (CanSend() && SellerId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_BUY).U32(SellerId));
	}
}

void UMRNetSubsystem::RequestWithdrawal(uint32 KeeperId)
{
	if (CanSend() && KeeperId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_WITHDRAWAL).U32(KeeperId));
	}
}

void UMRNetSubsystem::BuyItems(const TArray<FItemCount>& Items)
{
	if (!CanSend() || !World.Shop.Seller.Id || Items.Num() == 0)
	{
		return;
	}
	FMRWriter W(World.Shop.bWithdrawal ? MRMsg::BP_REQ_WITHDRAWAL_ITEMS : MRMsg::BP_REQ_BUY_ITEMS);
	W.U32(World.Shop.Seller.Id);
	WriteItems(W, Items);
	Connection->Send(W);
}

void UMRNetSubsystem::Offer(uint32 ToId, const TArray<FItemCount>& Items)
{
	if (!CanSend() || !ToId || Items.Num() == 0)
	{
		return;
	}
	FMRWriter W(MRMsg::BP_REQ_OFFER);
	W.U32(ToId);
	WriteItems(W, Items);
	Connection->Send(W);
	// the server shows what we offered (BP_OFFERED) once the other side takes the offer
	World.Trade = FMRNetTrade();
	World.Trade.bOurs = true;
	World.Trade.WithId = ToId;
	const FMRNetObject* To = World.Objects.Find(ToId);
	World.Trade.WithName = To ? To->Name : FString();
}

void UMRNetSubsystem::Deposit(uint32 ToId, const TArray<FItemCount>& Items)
{
	if (!CanSend() || !ToId || Items.Num() == 0)
	{
		return;
	}
	FMRWriter W(MRMsg::BP_REQ_DEPOSIT);
	W.U32(ToId);
	WriteItems(W, Items);
	Connection->Send(W);
	World.Trade = FMRNetTrade();
	World.Trade.bOurs = true;
	World.Trade.WithId = ToId;
	const FMRNetObject* To = World.Objects.Find(ToId);
	World.Trade.WithName = To ? To->Name : FString();
}

void UMRNetSubsystem::Counteroffer(const TArray<FItemCount>& Items)
{
	if (CanSend() && World.Trade.bOpen && !World.Trade.bOurs)
	{
		FMRWriter W(MRMsg::BP_REQ_COUNTEROFFER);
		WriteItems(W, Items);
		Connection->Send(W);
	}
}

void UMRNetSubsystem::AcceptOffer()
{
	if (CanSend() && World.Trade.bOpen && World.Trade.bAnswered)
	{
		Connection->Send(FMRWriter(MRMsg::BP_ACCEPT_OFFER));
		// done (user.kod UserAcceptOffer: the items change hands; nothing more comes for this offer)
		World.Trade = FMRNetTrade();
		OnTradeChanged.Broadcast();
	}
}

void UMRNetSubsystem::CancelOffer()
{
	if (CanSend() && World.Trade.bOpen)
	{
		Connection->Send(FMRWriter(MRMsg::BP_CANCEL_OFFER));
	}
	World.Trade = FMRNetTrade();
	OnTradeChanged.Broadcast();
}

void UMRNetSubsystem::BankDeposit(int32 Shillings)
{
	if (CanSend() && Shillings > 0)
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_DEPOSIT).I32(Shillings));
	}
}

void UMRNetSubsystem::BankWithdraw(int32 Shillings)
{
	if (CanSend() && Shillings > 0)
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_WITHDRAW).I32(Shillings));
	}
}

void UMRNetSubsystem::BankBalance()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_BALANCE));
	}
}

void UMRNetSubsystem::CastSpell(uint32 SpellId, const TArray<uint32>& Targets)
{
	if (!CanSend() || !SpellId)
	{
		return;
	}
	// clientd3d protocol.c: the spell's id, then PARAM_OBJECT_LIST (u16 count, the ids)
	FMRWriter W(MRMsg::BP_REQ_CAST);
	W.U32(SpellId).U16(static_cast<uint16>(Targets.Num()));
	for (const uint32 Id : Targets)
	{
		W.U32(Id);
	}
	Connection->Send(W);
	const FMRNetSpell* S = World.FindSpell(SpellId);
	UE_LOG(LogMeridian, Log, TEXT("MRNet: cast %s on %d target(s)"), S ? *S->Object.Name : TEXT("?"), Targets.Num());
}

void UMRNetSubsystem::Rest()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_REST));
	}
}

void UMRNetSubsystem::Stand()
{
	if (CanSend())
	{
		Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_STAND));
	}
}

void UMRNetSubsystem::ChangeStats(const uint8 (&Stats)[6])
{
	if (!CanSend())
	{
		return;
	}
	// stats.c: the six stats, then the school levels as they were offered (user.kod UserChangedStats)
	FMRWriter W(MRMsg::BP_CHANGED_STATS);
	for (const uint8 V : Stats)
	{
		W.U8(V);
	}
	for (const uint8 V : World.StatChange.Levels)
	{
		W.U8(V);
	}
	Connection->Send(W);
}

void UMRNetSubsystem::DebugEffect(uint16 Effect, int32 Ms, int32 Xlat)
{
	FMRWriter W(MRMsg::BP_EFFECT);
	W.U16(Effect);
	switch (Effect)
	{
	case MRMsg::EFFECT_INVERT:
	case MRMsg::EFFECT_SHAKE:
	case MRMsg::EFFECT_PAIN:
	case MRMsg::EFFECT_BLUR:
	case MRMsg::EFFECT_WAVER:
	case MRMsg::EFFECT_WHITEOUT:
		W.I32(Ms);
		break;
	case MRMsg::EFFECT_FLASHXLAT:
		W.I32(Ms).I32(Xlat);
		break;
	case MRMsg::EFFECT_XLATOVERRIDE:
		W.I32(Xlat);
		break;
	default:
		break;
	}
	HandleMessage(W.Bytes);
}

void UMRNetSubsystem::UseItem(uint32 ItemId)
{
	if (CanSend() && ItemId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_USE).U32(ItemId));
	}
}

void UMRNetSubsystem::UnuseItem(uint32 ItemId)
{
	if (CanSend() && ItemId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_UNUSE).U32(ItemId));
	}
}

void UMRNetSubsystem::Pickup(uint32 ObjectId)
{
	if (CanSend() && ObjectId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_GET).U32(ObjectId));
	}
}

void UMRNetSubsystem::PickupFromContainer(uint32 ItemId, uint32 Amount)
{
	if (CanSend() && ItemId)
	{
		FMRWriter W(MRMsg::BP_REQ_GET_FROM_CONTAINER);
		WriteItem(W, ItemId, Amount);
		Connection->Send(W);
	}
}

void UMRNetSubsystem::Drop(uint32 ItemId, uint32 Amount)
{
	if (CanSend() && ItemId)
	{
		FMRWriter W(MRMsg::BP_REQ_DROP);
		WriteItem(W, ItemId, Amount);
		Connection->Send(W);
	}
}

void UMRNetSubsystem::Put(uint32 ItemId, uint32 ContainerId, uint32 Amount)
{
	if (CanSend() && ItemId && ContainerId)
	{
		FMRWriter W(MRMsg::BP_REQ_PUT);
		WriteItem(W, ItemId, Amount);
		W.U32(ContainerId);
		Connection->Send(W);
	}
}

void UMRNetSubsystem::Apply(uint32 ItemId, uint32 TargetId)
{
	if (CanSend() && ItemId && TargetId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_APPLY).U32(ItemId).U32(TargetId));
	}
}

void UMRNetSubsystem::Activate(uint32 ObjectId)
{
	if (CanSend() && ObjectId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_REQ_ACTIVATE).U32(ObjectId));
	}
}

void UMRNetSubsystem::RequestContents(uint32 ContainerId)
{
	if (CanSend() && ContainerId)
	{
		Connection->Send(FMRWriter(MRMsg::BP_SEND_OBJECT_CONTENTS).U32(ContainerId));
	}
}

void UMRNetSubsystem::MoveInventoryItem(uint32 ItemId, uint32 PlaceOfId)
{
	if (CanSend() && ItemId && PlaceOfId && ItemId != PlaceOfId)
	{
		// user.kod UserMoveInventoryItem: the item takes the other's place; the server doesn't say
		// so, so ask for the list again rather than redo its list arithmetic here
		Connection->Send(FMRWriter(MRMsg::BP_REQ_INVENTORY_MOVE).U32(ItemId).U32(PlaceOfId));
		Connection->Send(FMRWriter(MRMsg::BP_REQ_INVENTORY));
	}
}

void UMRNetSubsystem::ChangeDescription(uint32 ObjectId, const FString& Text)
{
	if (Connection.IsValid() && Phase == EMRNetPhase::InGame && ObjectId && !IsWaiting())
	{
		// (clientd3d object.h MAX_DESCRIPTION)
		Connection->Send(FMRWriter(MRMsg::BP_CHANGE_DESCRIPTION).U32(ObjectId).Str(Text.Left(1000)));
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
		const TSharedPtr<FJsonObject>* Files = nullptr;
		Assets->SetManifest(Root->TryGetObjectField(TEXT("files"), Files) ? *Files : nullptr);
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

void UMRNetSubsystem::HandleUserCommand(FMRReader& R, const TArray<uint8>& Body)
{
	const uint8 Command = R.U8();
	switch (Command)
	{
	case MRMsg::UC_LOOK_PLAYER:
	{
		FMRNetDescription D;
		if (MRNetRead::LookPlayer(R, Resources, D))
		{
			Description = MoveTemp(D);
			OnDescription.Broadcast();
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: UC_LOOK_PLAYER couldn't be read (%d bytes)"), Body.Num());
		}
		break;
	}
	case MRMsg::UC_SEND_QUIT:
		// merintr.c HandleSendQuit: the server is done with this character (a suicide): back to the list
		UE_LOG(LogMeridian, Log, TEXT("MRNet: the server sends us to the character list"));
		ReturnToCharacters();
		break;
	case MRMsg::UC_GUILDINFO:
		if (MRNetRead::GuildInfo(R, World.Guild))
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: guild %s, %d members, rights %x"), *World.Guild.Name, World.Guild.Members.Num(), World.Guild.Flags);
			OnGuildChanged.Broadcast();
		}
		break;
	case MRMsg::UC_GUILD_LIST:
		if (MRNetRead::GuildList(R, World.GuildList))
		{
			OnGuildChanged.Broadcast();
		}
		break;
	case MRMsg::UC_GUILD_ASK:
		// a guild creator offers to found one (merintr.c HandleGuildAsk): two prices
		World.GuildCost = R.I32();
		World.GuildSecretCost = R.I32();
		if (R.IsOk())
		{
			OnGuildChanged.Broadcast();
		}
		break;
	case MRMsg::UC_RECEIVE_PREFERENCES:
		World.Preferences = R.U32();
		World.bHasPreferences = R.IsOk();
		UE_LOG(LogMeridian, Log, TEXT("MRNet: preferences %x"), World.Preferences);
		OnPreferencesChanged.Broadcast();
		break;
	default:
		break;  // halls and shields (docs/parity.md)
	}
}

void UMRNetSubsystem::AddChat(const FString& Text, uint8 Kind, EMRChatChannel Channel)
{
	FMRChatLine Line;
	Line.Text = MRServerText::StripStyle(Text);
	Line.Styled = Text;
	Line.Kind = Kind;
	Line.Channel = Channel;
	Line.Time = FPlatformTime::Seconds();
	Line.When = FDateTime::Now();
	UE_LOG(LogMeridian, Log, TEXT("MRNet chat: %s"), *Line.Text);
	Chat.Add(Line);
	if (Chat.Num() > MaxChatLines)
	{
		Chat.RemoveAt(0, Chat.Num() - MaxChatLines);
	}
	OnChat.Broadcast(Line);
}

const FMRNetStatGroup* UMRNetSubsystem::FindStatGroup(uint8 Group) const
{
	return World.FindStatGroup(Group);
}

void UMRNetSubsystem::SetWaiting(bool bInWaiting)
{
	const bool bWas = IsWaiting();
	bWaiting = bInWaiting;
	if (IsWaiting() != bWas)
	{
		OnWaitChanged.Broadcast();
	}
}

void UMRNetSubsystem::ReloadData()
{
	if (!Connection.IsValid() || Phase != EMRNetPhase::InGame)
	{
		return;
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: asking for the player, room, players and stats again"));
	const bool bWas = IsWaiting();
	World.ResetRoom();
	bAwaitingRoom = true;
	if (!bWas)
	{
		OnWaitChanged.Broadcast();
	}
	Connection->Send(FMRWriter(MRMsg::BP_SEND_PLAYER));
	Connection->Send(FMRWriter(MRMsg::BP_SEND_ROOM_CONTENTS));
	Connection->Send(FMRWriter(MRMsg::BP_SEND_PLAYERS));
	// a save renumbers objects, the spells and skills in the stat groups too, and what we carry
	Connection->Send(FMRWriter(MRMsg::BP_SEND_STAT_GROUPS));
	World.ResetInventory();
	OnInventoryChanged.Broadcast();
	Connection->Send(FMRWriter(MRMsg::BP_REQ_INVENTORY));
	// and the spells, skills and player enchantments (merintr InterfaceResetData)
	World.ResetAbilities();
	OnAbilitiesChanged.Broadcast();
	OnEnchantmentsChanged.Broadcast();
	Connection->Send(FMRWriter(MRMsg::BP_SEND_SPELLS));
	Connection->Send(FMRWriter(MRMsg::BP_SEND_SKILLS));
	Connection->Send(FMRWriter(MRMsg::BP_SEND_ENCHANTMENTS).U8(MRMsg::ENCHANT_PLAYER));
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
		if (MRNetRead::Player(R, Resources, World.Player))
		{
			bAwaitingRoom = true;
			World.RoomChanges.Reset();  // the server sends this room's changes next
			LoadCharacterData();
			// a new room (or the same one again): the last one's looping sounds end (SF_LOOP)
			FMRNetSound Stop;
			Stop.Kind = FMRNetSound::EKind::StopLoops;
			OnSound.Broadcast(Stop);
			if (World.RoomEnchantments.Num() > 0)
			{
				World.RoomEnchantments.Reset();  // merintr enchant.c EnchantmentsNewRoom
				OnEnchantmentsChanged.Broadcast();
			}
			UE_LOG(LogMeridian, Log, TEXT("MRNet: room %s (%s), security %08x, ambient light %d, background %s"), *World.Player.RoomFile,
				*World.Player.RoomName, World.Player.RoomSecurity, World.Player.AmbientLight, *World.Player.Background);
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: BP_PLAYER cut short (%d bytes)"), Body.Num());
		}
		break;
	case MRMsg::BP_ROOM_CONTENTS:
	{
		R.U32();  // room object
		const int32 N = R.U16();
		World.Objects.Reset();
		for (int32 i = 0; i < N; ++i)
		{
			FMRNetObject O;
			if (!MRNetRead::RoomObject(R, Resources, O))
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: room contents cut short at object %d of %d"), i, N);
				break;
			}
			World.Objects.Add(O.Id, MoveTemp(O));
		}
		const bool bWasWaiting = IsWaiting();
		bAwaitingRoom = false;
		++RoomsEntered;
		SetPhase(EMRNetPhase::InGame);
		OnRoomEntered.Broadcast();
		if (bWasWaiting && !IsWaiting())
		{
			OnWaitChanged.Broadcast();
		}
		if (!bRequestedStats)
		{
			// the stat groups' names first, then every group (merintr stats.c StatsGroupsInfo)
			bRequestedStats = true;
			Connection->Send(FMRWriter(MRMsg::BP_SEND_STAT_GROUPS));
			// the inventory and what is in use (clientd3d game.c asks on entering the game)
			Connection->Send(FMRWriter(MRMsg::BP_REQ_INVENTORY));
			// the spells, skills and enchantments on us (merintr mermain.c, enchant.c EnchantmentsInit)
			Connection->Send(FMRWriter(MRMsg::BP_SEND_SPELLS));
			// the server-kept options (merintr.c asks on entering; the safety commands change them)
			Connection->Send(FMRWriter(MRMsg::BP_USERCOMMAND).U8(MRMsg::UC_REQ_PREFERENCES));
			Connection->Send(FMRWriter(MRMsg::BP_SEND_SKILLS));
			Connection->Send(FMRWriter(MRMsg::BP_SEND_ENCHANTMENTS).U8(MRMsg::ENCHANT_PLAYER));
		}
		break;
	}
	case MRMsg::BP_STAT_GROUPS:
	{
		const int32 N = R.U8();
		for (int32 i = 1; i <= N && R.IsOk(); ++i)
		{
			FMRNetStatGroup& G = World.StatGroup(static_cast<uint8>(i));
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
			if (!MRNetRead::Stat(R, Resources, S))
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: stat group %d cut short at %d of %d"), Group, i, N);
				break;
			}
			Stats.Add(MoveTemp(S));
		}
		FMRNetStatGroup& G = World.StatGroup(Group);
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
		if (MRNetRead::Stat(R, Resources, S))
		{
			FMRNetStatGroup& G = World.StatGroup(Group);
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
		if (MRNetRead::RoomObject(R, Resources, O))
		{
			const uint32 Id = O.Id;
			World.Objects.Add(Id, MoveTemp(O));
			OnObjectAdded.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_REMOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		if (World.Objects.Remove(Id) > 0)
		{
			OnObjectRemoved.Broadcast(Id);
		}
		break;
	}
	case MRMsg::BP_CHANGE:
	{
		// the object and a new motion record (clientd3d server.c HandleChange); where it is stays
		FMRNetObject O;
		if (MRNetRead::Object(R, Resources, O) && MRNetRead::Motion(R, Resources, O))
		{
			// something we carry (user.kod SomethingChanged: a number item's new amount after a split)
			if (FMRNetObject* Carried = World.Inventory.FindByPredicate([&O](const FMRNetObject& E) { return E.Id == O.Id; }))
			{
				UE_LOG(LogMeridian, Log, TEXT("MRNet: inventory ~ %s%s"), O.bNumber ? *FString::Printf(TEXT("%u "), O.Amount) : TEXT(""), *O.Name);
				*Carried = O;
				OnInventoryChanged.Broadcast();
			}
			if (FMRNetObject* Existing = World.Objects.Find(O.Id))
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
		if (FMRNetObject* O = World.Objects.Find(Id); O && R.IsOk())
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
		if (FMRNetObject* O = World.Objects.Find(Id); O && R.IsOk())
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
		// everyone logged on; their names become resources (clientd3d server.c HandlePlayers)
		World.Users.Reset();
		const int32 N = R.U16();
		for (int32 i = 0; i < N; ++i)
		{
			FMRNetUser U;
			if (!MRNetRead::User(R, U))
			{
				UE_LOG(LogMeridian, Warning, TEXT("MRNet: players list cut short at %d of %d"), i, N);
				break;
			}
			Resources.SetDynamic(U.NameRsc, U.Name);
			World.Users.Add(U.Id, MoveTemp(U));
		}
		UE_LOG(LogMeridian, Log, TEXT("MRNet: %d players logged on"), World.Users.Num());
		OnUsersChanged.Broadcast();
		break;
	}
	case MRMsg::BP_PLAYER_ADD:
	{
		FMRNetUser U;
		if (MRNetRead::User(R, U))
		{
			Resources.SetDynamic(U.NameRsc, U.Name);
			World.Users.Add(U.Id, MoveTemp(U));
			OnUsersChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_PLAYER_REMOVE:
		if (World.Users.Remove(MRMsg::PlainId(R.U32())) > 0)
		{
			OnUsersChanged.Broadcast();
		}
		break;
	case MRMsg::BP_SAID:
	{
		const uint32 Sender = MRMsg::PlainId(R.U32());
		const FString SenderName = Resources.Get(R.U32());
		const uint8 Kind = R.U8();
		const uint32 FormatId = R.U32();
		// msgfiltr.c MessageSaid: a player's words are dropped when ignored (or everyone is, or broadcasts
		// are off); a tell so dropped is reported back (BP_SAY_BLOCKED)
		const bool bFromPlayer = Kind != MRMsg::SAY_RESOURCE && Sender != World.Player.Id;
		if (bFromPlayer && (Social.bIgnoreAll || IsIgnored(SenderName) || (Kind == MRMsg::SAY_EVERYONE && Social.bNoBroadcast)))
		{
			if (Kind == MRMsg::SAY_GROUP && (Social.bIgnoreAll || IsIgnored(SenderName)))
			{
				Connection->Send(FMRWriter(MRMsg::BP_SAY_BLOCKED).U32(Sender));
			}
			break;
		}
		FString Text;
		if (MRServerText::Format(Resources, FormatId, R, Text))
		{
			AddChat(Text, Kind, EMRChatChannel::Chat);
			if (Kind == MRMsg::SAY_GROUP && bFromPlayer)
			{
				// a tell: the original's ding
				FMRNetSound Ding;
				Ding.File = TEXT("imp.ogg");
				OnSound.Broadcast(Ding);
			}
		}
		break;
	}
	case MRMsg::BP_MESSAGE:
	case MRMsg::BP_SYS_MESSAGE:
	{
		const uint32 FormatId = R.U32();
		if (const int32* Kind = Body[0] == MRMsg::BP_MESSAGE ? HitFormats().Find(FormatId) : nullptr)
		{
			FMRNetHit Hit;
			if (MRNetRead::Hit(R, Resources, *Kind, Hit))
			{
				OnHit.Broadcast(Hit);
			}
		}
		FString Text;
		if (MRServerText::Format(Resources, FormatId, R, Text))
		{
			AddChat(Text, 0, HitFormats().Contains(FormatId) ? EMRChatChannel::Combat : EMRChatChannel::Game);
		}
		else
		{
			const FString* Fmt = Resources.Find(FormatId);
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: message %u couldn't be formatted (%d bytes): \"%s\""), FormatId, Body.Num(), Fmt ? **Fmt : TEXT("not in the rsb"));
		}
		break;
	}
	case MRMsg::BP_PASSWORD_OK:
	case MRMsg::BP_PASSWORD_NOT_OK:
	{
		// server.c HandlePasswordOk / NotOk (client.rc IDS_PASSWORDCHANGED, IDS_PASSWORDNOTCHANGED)
		const bool bOk = Body[0] == MRMsg::BP_PASSWORD_OK;
		AddChat(bOk ? TEXT("Password changed.") : TEXT("You typed your old password incorrectly.  Password NOT changed!"), 0);
		OnPasswordChanged.Broadcast(bOk);
		break;
	}
	case MRMsg::BP_MAIL:
	{
		FMRNetMail Mail;
		bool bEnd = false;
		if (!MRNetRead::Mail(R, Resources, Mail, bEnd))
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: BP_MAIL couldn't be read (%d bytes)"), Body.Num());
			break;
		}
		if (bEnd)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: no more new mail (%d kept)"), Mailbox.Num());
			OnMailChanged.Broadcast();
			break;
		}
		// mailfile.c: kept on this computer, then the server may delete it
		const uint32 Index = Mail.Index;
		UE_LOG(LogMeridian, Log, TEXT("MRNet: mail from %s: %s"), *Mail.From, *Mail.Subject);
		Mailbox.Add(MoveTemp(Mail));
		SaveMail();
		Connection->Send(FMRWriter(MRMsg::BP_DELETE_MAIL).U32(Index));
		OnMailChanged.Broadcast();
		break;
	}
	case MRMsg::BP_LOOKUP_NAMES:
	{
		TArray<uint32> Ids;
		if (!MRNetRead::LookupNames(R, Ids) || !IsSendingMail())
		{
			break;
		}
		const FPendingMail Mail = MoveTemp(PendingMail);
		PendingMail = FPendingMail();
		FString Unknown;
		for (int32 i = 0; i < Mail.To.Num(); ++i)
		{
			if (!Ids.IsValidIndex(i) || Ids[i] == 0)
			{
				Unknown = Mail.To[i];
				break;
			}
		}
		if (!Unknown.IsEmpty() || Ids.Num() != Mail.To.Num())
		{
			OnMailSent.Broadcast(false, FString::Printf(TEXT("There is no player named %s."), *Unknown));
			break;
		}
		// protocol.c PARAM_ID_ARRAY: u16 count, the ids; then the text ("Subject: ..." first)
		FMRWriter W(MRMsg::BP_SEND_MAIL);
		W.U16(static_cast<uint16>(Ids.Num()));
		for (const uint32 Id : Ids)
		{
			W.U32(Id);
		}
		Connection->Send(W.Str(Mail.Text));
		UE_LOG(LogMeridian, Log, TEXT("MRNet: mail sent to %s"), *FString::Join(Mail.To, TEXT(", ")));
		OnMailSent.Broadcast(true, FString());
		break;
	}
	case MRMsg::BP_LOOK_NEWSGROUP:
	{
		FMRNetNews News;
		if (MRNetRead::LookNewsgroup(R, Resources, News))
		{
			News.bOpen = true;
			World.News = MoveTemp(News);
			UE_LOG(LogMeridian, Log, TEXT("MRNet: news board %d (%s), permission %d"), World.News.Group, *World.News.Board.Name, World.News.Permission);
			OnNewsChanged.Broadcast();
			if (World.News.Permission & MRMsg::NEWS_READ)
			{
				RequestArticles();  // newsread.c: the list comes up at once
			}
		}
		break;
	}
	case MRMsg::BP_ARTICLES:
	{
		uint16 Group = 0;
		uint8 Part = 0, Parts = 0;
		TArray<FMRNetArticle> Got;
		if (MRNetRead::Articles(R, Group, Part, Parts, Got) && World.News.bOpen && Group == World.News.Group)
		{
			if (Part <= 1)
			{
				World.News.Articles.Reset();
			}
			World.News.Articles.Append(Got);
			World.News.bHaveArticles = Part >= Parts;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: news board %d: part %d of %d, %d articles"), Group, Part, Parts, World.News.Articles.Num());
			OnNewsChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_ARTICLE:
		World.News.ReadingText = R.Str();
		World.News.bHaveText = R.IsOk();
		OnNewsChanged.Broadcast();
		break;
	case MRMsg::BP_LOOK:
	{
		FMRNetDescription D;
		if (MRNetRead::Look(R, Resources, D))
		{
			Description = MoveTemp(D);
			OnDescription.Broadcast();
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: BP_LOOK couldn't be read (%d bytes)"), Body.Num());
		}
		break;
	}
	case MRMsg::BP_USERCOMMAND:
		HandleUserCommand(R, Body);
		break;
	case MRMsg::BP_PLAYER_OVERLAY:
	{
		// clientd3d overlay.c SetPlayerOverlay: the object's id is the slot (PWO_*), replaced each time;
		// hotspot 0 hides it (an unused weapon), so does group 0 (the fist after its swing)
		const uint8 Hotspot = R.U8();
		FMRNetObject O;
		if (MRNetRead::ObjectNoLight(R, Resources, O))
		{
			FMRNetPlayerOverlay& P = World.PlayerOverlays.FindOrAdd(O.Id);
			P.Hotspot = Hotspot;
			P.Object = MoveTemp(O);
			P.Seq = ++World.PlayerOverlaySeq;
			UE_LOG(LogMeridian, Verbose, TEXT("MRNet: first person slot %u: %s at %d"), P.Object.Id, *P.Object.Icon, Hotspot);
		}
		break;
	}
	case MRMsg::BP_SPELLS:
		if (MRNetRead::SpellList(R, Resources, World.Spells))
		{
			World.bHasSpells = true;
			TArray<FString> Names;
			for (const FMRNetSpell& S : World.Spells)
			{
				Names.Add(S.Object.Name);
			}
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %d spells: %s"), World.Spells.Num(), *FString::Join(Names, TEXT(", ")));
			OnAbilitiesChanged.Broadcast();
		}
		break;
	case MRMsg::BP_SPELL_ADD:
	{
		FMRNetSpell S;
		if (MRNetRead::Spell(R, Resources, S))
		{
			World.Spells.RemoveAll([&S](const FMRNetSpell& E) { return E.Object.Id == S.Object.Id; });
			World.Spells.Add(MoveTemp(S));
			OnAbilitiesChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_SPELL_REMOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		World.Spells.RemoveAll([Id](const FMRNetSpell& E) { return E.Object.Id == Id; });
		OnAbilitiesChanged.Broadcast();
		break;
	}
	case MRMsg::BP_SKILLS:
		if (MRNetRead::ObjectList(R, Resources, World.Skills))
		{
			World.bHasSkills = true;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %d skills"), World.Skills.Num());
			OnAbilitiesChanged.Broadcast();
		}
		break;
	case MRMsg::BP_SKILL_ADD:
	{
		FMRNetObject O;
		if (MRNetRead::Object(R, Resources, O))
		{
			World.Skills.RemoveAll([&O](const FMRNetObject& E) { return E.Id == O.Id; });
			World.Skills.Add(MoveTemp(O));
			OnAbilitiesChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_SKILL_REMOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		World.Skills.RemoveAll([Id](const FMRNetObject& E) { return E.Id == Id; });
		OnAbilitiesChanged.Broadcast();
		break;
	}
	case MRMsg::BP_ADD_ENCHANTMENT:
	{
		// merintr.c HandleAddEnchantment: the type, then the enchantment as an object (its spell's name and icon)
		const uint8 Type = R.U8();
		FMRNetObject O;
		if (MRNetRead::Object(R, Resources, O) && (Type == MRMsg::ENCHANT_PLAYER || Type == MRMsg::ENCHANT_ROOM))
		{
			TArray<FMRNetObject>& List = Type == MRMsg::ENCHANT_PLAYER ? World.PlayerEnchantments : World.RoomEnchantments;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: enchantment on the %s: %s (%s)"), Type == MRMsg::ENCHANT_PLAYER ? TEXT("player") : TEXT("room"), *O.Name, *O.Icon);
			List.RemoveAll([&O](const FMRNetObject& E) { return E.Id == O.Id; });
			List.Add(MoveTemp(O));
			OnEnchantmentsChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_REMOVE_ENCHANTMENT:
	{
		const uint8 Type = R.U8();
		const uint32 Id = MRMsg::PlainId(R.U32());
		TArray<FMRNetObject>& List = Type == MRMsg::ENCHANT_PLAYER ? World.PlayerEnchantments : World.RoomEnchantments;
		if (List.RemoveAll([Id](const FMRNetObject& E) { return E.Id == Id; }) > 0)
		{
			OnEnchantmentsChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_LIGHT_AMBIENT:
	case MRMsg::BP_LIGHT_PLAYER:
	{
		// server.c HandleLightAmbient / HandleLightPlayer: one byte (the room's light, the player's own)
		const uint8 Light = R.U8();
		if (R.IsOk())
		{
			(Body[0] == MRMsg::BP_LIGHT_AMBIENT ? World.Player.AmbientLight : World.Player.PlayerLight) = Light;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %s light %d"), Body[0] == MRMsg::BP_LIGHT_AMBIENT ? TEXT("ambient") : TEXT("player"), Light);
			OnLightChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_LIGHT_SHADING:
	{
		// HandleLightShading: the sun's strength, angle and height (user.kod ToCliShading)
		const uint8 Directional = R.U8();
		const uint16 Angle = R.U16();
		const uint16 Height = R.U16();
		if (R.IsOk())
		{
			World.Player.DirectionalLight = Directional;
			World.Player.SunAngle = Angle;
			World.Player.SunHeight = Height;
			OnLightChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_SECTOR_LIGHT:
		// flickering on or off (SL_FLICKER_*): the original's Direct3D client doesn't draw it, nor do we
		break;
	case MRMsg::BP_SECTOR_MOVE:
	case MRMsg::BP_SECTOR_CHANGE:
	case MRMsg::BP_CHANGE_TEXTURE:
	{
		FMRNetRoomChange C;
		if (MRNetRead::RoomChange(Body[0], R, C))
		{
			World.RoomChanges.Add(C);
			OnRoomChange.Broadcast(C);
		}
		break;
	}
	case MRMsg::BP_PLAY_WAVE:
	{
		// server.c HandlePlayWave: rsc, object, flags, row, col, radius and volume (both ignored, as there)
		FMRNetSound S;
		S.File = Resources.Get(R.U32());
		S.ObjectId = MRMsg::PlainId(R.U32());
		S.Flags = R.U8();
		S.Row = R.I32();
		S.Col = R.I32();
		R.I32();
		R.I32();
		if (R.IsOk() && !S.File.IsEmpty())
		{
			OnSound.Broadcast(S);
		}
		break;
	}
	case MRMsg::BP_STOP_WAVE:
	{
		FMRNetSound S;
		S.Kind = FMRNetSound::EKind::Stop;
		S.File = Resources.Get(R.U32());
		S.ObjectId = MRMsg::PlainId(R.U32());
		if (R.IsOk() && !S.File.IsEmpty())
		{
			OnSound.Broadcast(S);
		}
		break;
	}
	case MRMsg::BP_PLAY_MUSIC:
	case MRMsg::BP_PLAY_MIDI:
	{
		FMRNetSound S;
		S.Kind = FMRNetSound::EKind::Music;
		S.File = Resources.Get(R.U32());
		if (R.IsOk())
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: music %s"), S.File.IsEmpty() ? TEXT("(none)") : *S.File);
			OnSound.Broadcast(S);
		}
		break;
	}
	case MRMsg::BP_BUY_LIST:
	case MRMsg::BP_WITHDRAWAL_LIST:
		// (the original doesn't check a buy list's length: Server 104 may send more after it)
		if (MRNetRead::BuyList(R, Resources, World.Shop))
		{
			World.Shop.bWithdrawal = Body[0] == MRMsg::BP_WITHDRAWAL_LIST;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %s %s: %d items"), *World.Shop.Seller.Name,
				World.Shop.bWithdrawal ? TEXT("holds") : TEXT("sells"), World.Shop.Items.Num());
			OnShop.Broadcast();
		}
		break;
	case MRMsg::BP_OFFER:
	{
		// someone offers to us: who, and what (server.c HandleOffer)
		FMRNetObject Who;
		TArray<FMRNetObject> Items;
		if (MRNetRead::Object(R, Resources, Who) && MRNetRead::ObjectList(R, Resources, Items))
		{
			World.Trade = FMRNetTrade();
			World.Trade.bOpen = true;
			World.Trade.WithId = Who.Id;
			World.Trade.WithName = Who.Name;
			World.Trade.Received = MoveTemp(Items);
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %s offers %d items"), *Who.Name, World.Trade.Received.Num());
			OnTradeChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_OFFERED:
	case MRMsg::BP_COUNTEROFFER:
	case MRMsg::BP_COUNTEROFFERED:
	{
		TArray<FMRNetObject> Items;
		if (MRNetRead::ObjectList(R, Resources, Items))
		{
			FMRNetTrade& T = World.Trade;
			T.bOpen = true;
			if (Body[0] == MRMsg::BP_COUNTEROFFER)
			{
				T.Received = MoveTemp(Items);  // their answer to our offer
				T.bAnswered = true;
			}
			else
			{
				T.Given = MoveTemp(Items);     // what we offer, or answered with
				T.bCountered = Body[0] == MRMsg::BP_COUNTEROFFERED;
			}
			UE_LOG(LogMeridian, Log, TEXT("MRNet: offer with %s: we give %d, they give %d%s"), *T.WithName, T.Given.Num(), T.Received.Num(),
				T.bAnswered ? TEXT(" (answered)") : TEXT(""));
			OnTradeChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_OFFER_CANCELED:
		UE_LOG(LogMeridian, Log, TEXT("MRNet: the offer is over"));
		World.Trade = FMRNetTrade();
		OnTradeChanged.Broadcast();
		break;
	case MRMsg::BP_REQ_STAT_CHANGE:
		if (MRNetRead::StatChange(R, World.StatChange))
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: the server offers retraining"));
			OnStatChange.Broadcast();
		}
		break;
	case MRMsg::BP_CHANGED_STATS_OK:
	case MRMsg::BP_CHANGED_STATS_NOT_OK:
		OnStatChangeResult.Broadcast(Body[0] == MRMsg::BP_CHANGED_STATS_OK);
		break;
	case MRMsg::BP_EFFECT:
		if (World.Effects.Apply(R))
		{
			OnEffect.Broadcast();
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: BP_EFFECT not understood (%d bytes)"), Body.Num());
		}
		break;
	case MRMsg::BP_SHOOT:
	case MRMsg::BP_RADIUS_SHOOT:
	{
		FMRNetProjectile P;
		if (MRNetRead::Projectile(R, Resources, Body[0] == MRMsg::BP_RADIUS_SHOOT, P))
		{
			OnProjectile.Broadcast(P);
		}
		break;
	}
	case MRMsg::BP_INVENTORY:
	{
		TArray<FMRNetObject> List;
		if (MRNetRead::ObjectList(R, Resources, List))
		{
			World.Inventory = MoveTemp(List);
			World.bHasInventory = true;
			UE_LOG(LogMeridian, Log, TEXT("MRNet: carrying %d items"), World.Inventory.Num());
			OnInventoryChanged.Broadcast();
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: BP_INVENTORY cut short (%d bytes)"), Body.Num());
		}
		break;
	}
	case MRMsg::BP_INVENTORY_ADD:
	{
		FMRNetObject O;
		if (MRNetRead::Object(R, Resources, O))
		{
			// (a number item that grew comes again with its new amount)
			UE_LOG(LogMeridian, Log, TEXT("MRNet: inventory + %s%s"), O.bNumber ? *FString::Printf(TEXT("%u "), O.Amount) : TEXT(""), *O.Name);
			if (FMRNetObject* Had = World.Inventory.FindByPredicate([&O](const FMRNetObject& E) { return E.Id == O.Id; }))
			{
				*Had = MoveTemp(O);
			}
			else
			{
				World.Inventory.Add(MoveTemp(O));
			}
			OnInventoryChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_INVENTORY_REMOVE:
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		if (const FMRNetObject* Gone = World.FindInventory(Id))
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: inventory - %s"), *Gone->Name);
		}
		World.Inventory.RemoveAll([Id](const FMRNetObject& E) { return E.Id == Id; });
		World.Using.Remove(Id);
		OnInventoryChanged.Broadcast();
		break;
	}
	case MRMsg::BP_USE_LIST:
	{
		TArray<uint32> Ids;
		if (MRNetRead::IdList(R, Ids))
		{
			World.Using = TSet<uint32>(Ids);
			OnInventoryChanged.Broadcast();
		}
		break;
	}
	case MRMsg::BP_USE:
		World.Using.Add(MRMsg::PlainId(R.U32()));
		OnInventoryChanged.Broadcast();
		break;
	case MRMsg::BP_UNUSE:
		World.Using.Remove(MRMsg::PlainId(R.U32()));
		OnInventoryChanged.Broadcast();
		break;
	case MRMsg::BP_OBJECT_CONTENTS:
	{
		const uint32 Container = MRMsg::PlainId(R.U32());
		TArray<FMRNetObject> List;
		if (MRNetRead::ObjectList(R, Resources, List))
		{
			World.ContentsOf = Container;
			World.Contents = MoveTemp(List);
			UE_LOG(LogMeridian, Log, TEXT("MRNet: %u holds %d things"), Container, World.Contents.Num());
			OnContents.Broadcast();
		}
		break;
	}
	case MRMsg::BP_WAIT:
		// the server is saving; its objects are renumbered after (user.kod GarbageCollecting)
		SetWaiting(true);
		break;
	case MRMsg::BP_UNWAIT:
		SetWaiting(false);
		break;
	case MRMsg::BP_INVALIDATE_DATA:
		ReloadData();
		break;
	case MRMsg::BP_QUIT:
		// out of the game, still connected: the connection asks for the game again and the character
		// list follows (the original went back to its main menu: clientd3d game.c GameQuit)
		UE_LOG(LogMeridian, Log, TEXT("MRNet: left the game"));
		World.Reset();
		bRequestedStats = false;
		bAwaitingRoom = false;
		bWaiting = false;
		SetPhase(EMRNetPhase::Connecting, TEXT("Loading characters..."));
		break;
	default:
		UE_LOG(LogMeridian, Verbose, TEXT("MRNet: message %d not handled (%d bytes)"), Body[0], Body.Num());
		break;  // not handled yet: docs/parity.md says which milestone does
	}
}
