#include "UI/SMRAdminConsole.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRControls.h"
#include "UI/SMRHUD.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Zones/MRZoneSubsystem.h"

#define LOCTEXT_NAMESPACE "MRAdminConsole"

namespace
{
	const FLinearColor Dim(0.72f, 0.7f, 0.64f);
	const FLinearColor Warn(1.f, 0.55f, 0.45f);
	constexpr float WidthPx = 340.f;
	constexpr float LabelPx = 52.f;
	/** Where a row of buttons wraps (a wrap box sized by its slot reports a wrong height). */
	constexpr float RowWidthPx = WidthPx - LabelPx - 4.f;
	constexpr int32 HistoryMax = 30;

	/** A room to go to (data/net/rooms.json, from Kod: tools/kod_extract). */
	struct FRoomEntry
	{
		int32 Rid = 0;
		FString Name;
		FString Roo;
		FString Row;  // as the list shows it
	};

	const TArray<FRoomEntry>& Rooms()
	{
		static TArray<FRoomEntry> List;
		static bool bLoaded = false;
		if (bLoaded)
		{
			return List;
		}
		bLoaded = true;
		const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("net"), TEXT("rooms.json"));
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return List;
		}
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Root->TryGetArrayField(TEXT("rooms"), Array))
		{
			return List;
		}
		for (const TSharedPtr<FJsonValue>& V : *Array)
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (!R.IsValid())
			{
				continue;
			}
			FRoomEntry E;
			E.Rid = static_cast<int32>(R->GetNumberField(TEXT("rid")));
			R->TryGetStringField(TEXT("name"), E.Name);
			R->TryGetStringField(TEXT("roo"), E.Roo);
			E.Row = FString::Printf(TEXT("%-5d %s  (%s)"), E.Rid, *E.Name, *E.Roo);
			List.Add(MoveTemp(E));
		}
		List.Sort([](const FRoomEntry& A, const FRoomEntry& B) { return A.Rid < B.Rid; });
		return List;
	}

	/** A row: its label on the left, the controls after (as the Options window's). */
	TSharedRef<SWidget> Row(UMRUIStyle* S, const FText& Label, const TSharedRef<SWidget>& Controls)
	{
		const float Px = S->Px();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(LabelPx * Px)[MRUI::Label(S, Label, 9.f, false, Dim)]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Controls];
	}

	/** Who may press a button: anyone with the DM module (DM commands), or only an admin (server commands). */
	enum class ENeeds : uint8 { Staff, Admin };
}

SMRAdminConsole::~SMRAdminConsole()
{
	if (UMRNetSubsystem* N = Net())
	{
		N->OnAdminText.Remove(AdminTextHandle);
	}
}

void SMRAdminConsole::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Admin);
	if (UMRNetSubsystem* N = Net())
	{
		AdminTextHandle = N->OnAdminText.AddSP(this, &SMRAdminConsole::OnAdminText);
		UsersHandle = N->OnUsersChanged.AddSP(this, &SMRAdminConsole::RefreshUsers);  // (removed by SMRSocialWindow)
	}
	OutputStyle = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	// (built when opened: OnOpened; SharedThis isn't ready inside Construct)
}

void SMRAdminConsole::OnOpened()
{
	Status.Reset();
	bConfirmClear = false;
	Rebuild();
	OnAdminText();
	if (CommandField.IsValid())
	{
		CommandField->Focus();
	}
}

void SMRAdminConsole::SetTab(const FString& InTab)
{
	Tab = InTab;
	Status.Reset();
	bConfirmClear = false;
	Rebuild();
}

uint32 SMRAdminConsole::Me() const
{
	const UMRNetSubsystem* N = Net();
	return N ? N->GetPlayer().Id : 0;
}

uint32 SMRAdminConsole::PickedUser() const
{
	const int32 i = UserList.IsValid() ? UserList->GetSelected() : INDEX_NONE;
	return ShownUsers.IsValidIndex(i) ? ShownUsers[i] : 0;
}

int32 SMRAdminConsole::PickedRoom() const
{
	const int32 i = RoomList.IsValid() ? RoomList->GetSelected() : INDEX_NONE;
	if (ShownRooms.IsValidIndex(i))
	{
		return ShownRooms[i];
	}
	// nothing picked: a number typed in the search is a room (an RID the list doesn't know is fine)
	const FString Typed = RoomSearch.IsValid() ? RoomSearch->GetText().TrimStartAndEnd() : FString();
	return Typed.IsNumeric() ? FCString::Atoi(*Typed) : 0;
}

// ------------------------------------------------------------------------------ sending

void SMRAdminConsole::Remember(const FString& Line)
{
	History.Remove(Line);
	History.Add(Line);
	if (History.Num() > HistoryMax)
	{
		History.RemoveAt(0);
	}
	HistoryAt = History.Num();
}

void SMRAdminConsole::Admin(const FString& Command)
{
	UMRNetSubsystem* N = Net();
	if (!N || !N->IsAdmin())
	{
		Status = TEXT("Server commands need an admin account.");
		return;
	}
	Status.Reset();
	Remember(Command);
	N->AdminCommand(Command);
}

void SMRAdminConsole::DM(const FString& Text)
{
	UMRNetSubsystem* N = Net();
	if (!N || !N->IsStaff())
	{
		Status = TEXT("DM commands need a DM or admin character.");
		return;
	}
	Status.Reset();
	Remember(TEXT("dm ") + Text);
	N->DMSay(Text);
}

void SMRAdminConsole::RunLine(const FString& InLine)
{
	const FString Line = InLine.TrimStartAndEnd();
	if (Line.IsEmpty())
	{
		return;
	}
	// admindlg.c IDOK: "quit" closes the window (and forgets its text)
	if (Line.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		if (UMRNetSubsystem* N = Net())
		{
			N->ClearAdminText();
		}
		Close();
		return;
	}
	if (Line.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		if (UMRNetSubsystem* N = Net())
		{
			N->ClearAdminText();
		}
		return;
	}
	// module/dm's "dm" command: the rest goes as SAY_DM
	if (Line.StartsWith(TEXT("dm "), ESearchCase::IgnoreCase))
	{
		DM(Line.Mid(3));
		return;
	}
	Admin(Line);
}

void SMRAdminConsole::GoToRoom(int32 Rid)
{
	UMRNetSubsystem* N = Net();
	if (!N || Rid <= 0)
	{
		Status = TEXT("Pick a room, or type its number.");
		return;
	}
	if (N->IsAdmin())
	{
		// the original's Go to room (admindlg.c IDC_TELEPORT); user.kod TeleportTo
		Admin(FString::Printf(TEXT("send object %u teleportto rid int %d"), Me(), Rid));
	}
	else
	{
		// module/dm "goroom" (BP_REQ_DM: the server's RIGHTS_GOROOM settings decide)
		Status.Reset();
		Remember(FString::Printf(TEXT("goroom %d"), Rid));
		N->DMCommand(MRMsg::DM_CMD_GO_ROOM, FString::FromInt(Rid));
	}
}

void SMRAdminConsole::OnAdminText()
{
	const UMRNetSubsystem* N = Net();
	if (Output.IsValid())
	{
		Output->SetText(FText::FromString(N ? N->GetAdminText() : FString()));
		Output->ScrollTo(ETextLocation::EndOfDocument);  // the newest in view (admindlg.c EditBoxScroll)
	}
}

FReply SMRAdminConsole::OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	// Up and Down in the command line: what was sent before (the original's combo box)
	if (CommandField.IsValid() && CommandField->HasFocus() && History.Num() > 0
		&& (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down))
	{
		HistoryAt = FMath::Clamp(HistoryAt + (Event.GetKey() == EKeys::Up ? -1 : 1), 0, History.Num());
		CommandField->SetText(History.IsValidIndex(HistoryAt) ? History[HistoryAt] : FString());
		CommandField->MoveToEnd();
		return FReply::Handled();
	}
	return SMRSocialWindow::OnPreviewKeyDown(Geo, Event);
}

// ------------------------------------------------------------------------------ lists

void SMRAdminConsole::FilterRooms()
{
	if (!RoomList.IsValid())
	{
		return;
	}
	const FString Typed = RoomSearch.IsValid() ? RoomSearch->GetText().TrimStartAndEnd() : FString();
	TArray<FText> Items;
	ShownRooms.Reset();
	for (const FRoomEntry& R : Rooms())
	{
		if (Typed.IsEmpty() || R.Name.Contains(Typed) || R.Roo.Contains(Typed) || FString::FromInt(R.Rid).StartsWith(Typed))
		{
			Items.Add(FText::FromString(R.Row));
			ShownRooms.Add(R.Rid);
		}
	}
	RoomList->SetItems(Items);
	RoomList->SetSelected(INDEX_NONE);
}

void SMRAdminConsole::RefreshUsers()
{
	const UMRNetSubsystem* N = Net();
	if (!UserList.IsValid() || !N)
	{
		return;
	}
	const uint32 Was = PickedUser();
	TArray<FMRNetUser> Users;
	N->GetUsers().GenerateValueArray(Users);
	Users.Sort([](const FMRNetUser& A, const FMRNetUser& B) { return A.Name < B.Name; });
	TArray<FText> Items;
	ShownUsers.Reset();
	int32 Keep = INDEX_NONE;
	for (const FMRNetUser& U : Users)
	{
		if (U.Id == Was)
		{
			Keep = ShownUsers.Num();
		}
		Items.Add(FText::FromString(U.Id == Me() ? U.Name + TEXT("  (you)") : U.Name));
		ShownUsers.Add(U.Id);
	}
	UserList->SetItems(Items);
	UserList->SetSelected(Keep);
}

// ------------------------------------------------------------------------------ pages

TSharedRef<SWidget> SMRAdminConsole::MakeTravel()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRAdminConsole> Weak = SharedThis(this);
	auto Go = [Weak]()
	{
		if (const TSharedPtr<SMRAdminConsole> D = Weak.Pin())
		{
			D->GoToRoom(D->PickedRoom());
		}
	};
	SAssignNew(RoomSearch, SMRTextField, Ui).Width(150.f).MaxLength(40).HintText(LOCTEXT("RoomHint", "Name, room file or number"))
		.OnChanged(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->FilterRooms(); }))
		.OnSubmit(FSimpleDelegate::CreateLambda(Go));
	SAssignNew(RoomList, SMRSelectList, Ui).Width(WidthPx - 12.f).Height(92.f).TextSize(8.5f)
		.OnActivated(SMRSelectList::FOnRow::CreateLambda([Go](int32) { Go(); }));
	FilterRooms();
	TSharedRef<SHorizontalBox> Top = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[RoomSearch.ToSharedRef()]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("GoRoom", "Go there")).TextSize(9.f).MinWidth(44.f)
				.OnClicked(FSimpleDelegate::CreateLambda(Go))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Where", "Where am I?")).TextSize(9.f).MinWidth(50.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]()
				{
					if (TSharedPtr<SMRAdminConsole> D = Weak.Pin())
					{
						D->DM(TEXT("get roo"));     // dm.kod: the room's name, file and number
						D->DM(TEXT("get coords"));  // and our square, fine position and floor height
					}
				}))
		];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Top]
		+ SVerticalBox::Slot().AutoHeight()[RoomList.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
		[
			MRUI::Label(S, LOCTEXT("TravelHint", "Double-click a room to go there. Rooms we haven't built are built from the server's files."), 8.f, false, Dim)
		];
}

TSharedRef<SWidget> SMRAdminConsole::MakePlayers()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRAdminConsole> Weak = SharedThis(this);
	SAssignNew(UserList, SMRSelectList, Ui).Width(150.f).Height(110.f).TextSize(9.f);
	RefreshUsers();
	// admindlg.c IDC_SHOW, IDC_GOTO, IDC_RESCUE; GameDMCommand's getplayer
	auto ForUser = [Weak](TFunction<void(SMRAdminConsole&, uint32)> Do)
	{
		return [Weak, Do]()
		{
			const TSharedPtr<SMRAdminConsole> D = Weak.Pin();
			if (!D)
			{
				return;
			}
			if (const uint32 Id = D->PickedUser())
			{
				Do(*D, Id);
			}
			else
			{
				D->Status = TEXT("Pick a player first.");
			}
		};
	};
	TSharedRef<SVerticalBox> Buttons = SNew(SVerticalBox);
	auto Add = [&](const FText& Text, TFunction<void()> Do, const FText& Tip)
	{
		Buttons->AddSlot().AutoHeight().Padding(0.f, 1.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SMRTextButton, Ui).Text(Text).TextSize(9.f).MinWidth(54.f).OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(Do)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(3.f * Px, 0.f, 0.f, 0.f)
			[
				MRUI::Label(S, Tip, 8.f, false, Dim)
			]
		];
	};
	Add(LOCTEXT("Show", "Show"), ForUser([](SMRAdminConsole& D, uint32 Id) { D.Admin(FString::Printf(TEXT("show object %u"), Id)); }),
		LOCTEXT("ShowTip", "Every property of their character"));
	Add(LOCTEXT("GoTo", "Go to"), ForUser([](SMRAdminConsole& D, uint32 Id)
	{
		UMRNetSubsystem* N = D.Net();
		if (N && N->IsAdmin())
		{
			D.Admin(FString::Printf(TEXT("send object %u admingotoobject what object %u"), D.Me(), Id));
		}
		else if (N)
		{
			N->DMCommand(MRMsg::DM_CMD_GO_PLAYER, FString::FromInt(Id));
		}
	}), LOCTEXT("GoToTip", "Teleport to them"));
	Add(LOCTEXT("Bring", "Bring here"), ForUser([](SMRAdminConsole& D, uint32 Id)
	{
		UMRNetSubsystem* N = D.Net();
		if (N && N->IsAdmin())
		{
			D.Admin(FString::Printf(TEXT("send object %u admingotoobject what object %u"), Id, D.Me()));
		}
		else if (N)
		{
			N->DMCommand(MRMsg::DM_CMD_GET_PLAYER, FString::FromInt(Id));
		}
	}), LOCTEXT("BringTip", "Teleport them to you"));
	Add(LOCTEXT("Rescue", "Rescue"), ForUser([](SMRAdminConsole& D, uint32 Id) { D.Admin(FString::Printf(TEXT("send object %u admingotosafety"), Id)); }),
		LOCTEXT("RescueTip", "Send them somewhere safe"));
	Add(LOCTEXT("Tell", "Tell"), ForUser([](SMRAdminConsole& D, uint32 Id)
	{
		UMRNetSubsystem* N = D.Net();
		const FMRNetUser* U = N ? N->GetUsers().Find(Id) : nullptr;
		if (U && D.UI.IsValid())
		{
			const FString Name = U->Name;
			UMRUISubsystem* Ui = D.UI.Get();
			Ui->SetWindowOpen(EMRWindow::Admin, false);
			Ui->OpenChatWith(FString::Printf(TEXT("tell \"%s\" "), *Name));
		}
	}), LOCTEXT("TellTip", "Write to them"));
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[UserList.ToSharedRef()]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(5.f * Px, 0.f, 0.f, 0.f)[Buttons];
}

TSharedRef<SWidget> SMRAdminConsole::MakeSelf()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRAdminConsole> Weak = SharedThis(this);
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(Net());
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Wrap = [&]() { return SNew(SWrapBox).PreferredSize(RowWidthPx * Px).InnerSlotPadding(FVector2D(2.f * Px, 2.f * Px)); };
	auto Button = [&](const TSharedRef<SWrapBox>& To, const FText& Text, TFunction<void()> Do, ENeeds Needs = ENeeds::Staff)
	{
		To->AddSlot()
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(8.5f).MinWidth(38.f)
				.IsEnabled_Lambda([WeakNet, Needs]() { return WeakNet.IsValid() && (Needs == ENeeds::Admin ? WeakNet->IsAdmin() : WeakNet->IsStaff()); })
				.OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(Do)))
		];
	};
	auto Say = [Weak](const TCHAR* Text) { FString T = Text; return [Weak, T]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->DM(T); }; };
	auto AddRow = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[Row(S, Label, W)]; };

	// the DM say commands (dm.kod UserSay, admin.kod UserSay); "dm help" lists them all
	TSharedRef<SWrapBox> Life = Wrap();
	Button(Life, LOCTEXT("Immortal", "Immortal"), Say(TEXT("immortal")));
	Button(Life, LOCTEXT("Mortal", "Mortal"), Say(TEXT("mortal")));
	Button(Life, LOCTEXT("Boost", "Boost stats"), Say(TEXT("boost stats")));
	Button(Life, LOCTEXT("Spells", "Get spells"), Say(TEXT("get spells")));
	Button(Life, LOCTEXT("Skills", "Get skills"), Say(TEXT("get skills")));
	Button(Life, LOCTEXT("ClearAbilities", "Clear abilities"), Say(TEXT("clear abilities")));
	AddRow(LOCTEXT("Powers", "Powers"), Life);

	TSharedRef<SWrapBox> Seen = Wrap();
	Button(Seen, LOCTEXT("Hidden", "Hidden"), Say(TEXT("hidden")));
	Button(Seen, LOCTEXT("Blank", "Invisible"), Say(TEXT("blank")));
	Button(Seen, LOCTEXT("Plain", "Plain"), Say(TEXT("plain")));
	Button(Seen, LOCTEXT("StealthOn", "Stealth on"), Say(TEXT("stealth on")));
	Button(Seen, LOCTEXT("StealthOff", "Stealth off"), Say(TEXT("stealth off")));
	Button(Seen, LOCTEXT("Anonymous", "Anonymous"), Say(TEXT("anonymous")), ENeeds::Admin);
	AddRow(LOCTEXT("Seen", "Seen"), Seen);

	TSharedRef<SWrapBox> Karma = Wrap();
	Button(Karma, LOCTEXT("Good", "Good"), Say(TEXT("good")));
	Button(Karma, LOCTEXT("Neutral", "Neutral"), Say(TEXT("neutral")));
	Button(Karma, LOCTEXT("Evil", "Evil"), Say(TEXT("evil")));
	Button(Karma, LOCTEXT("AppealOn", "Appeals on"), Say(TEXT("appeal on")));
	Button(Karma, LOCTEXT("AppealOff", "Appeals off"), Say(TEXT("appeal off")));
	AddRow(LOCTEXT("Karma", "Karma"), Karma);

	auto Field = [&](TSharedPtr<SMRTextField>& Out, const FText& Hint, const TCHAR* Command)
	{
		const FString Cmd = Command;
		TFunction<void()> Do = [Weak, &Out, Cmd]()
		{
			const TSharedPtr<SMRAdminConsole> D = Weak.Pin();
			if (D && Out.IsValid() && !Out->GetText().TrimStartAndEnd().IsEmpty())
			{
				D->DM(Cmd + TEXT(" ") + Out->GetText().TrimStartAndEnd());
			}
		};
		SAssignNew(Out, SMRTextField, Ui).Width(130.f).MaxLength(60).HintText(Hint).OnSubmit(FSimpleDelegate::CreateLambda(Do));
		return Do;
	};
	TSharedRef<SWrapBox> Items = Wrap();
	TFunction<void()> GetItem = Field(ItemField, LOCTEXT("ItemHint", "Item name (\"long sword\")"), TEXT("item"));
	Items->AddSlot()[ItemField.ToSharedRef()];
	Button(Items, LOCTEXT("GetItem", "Get item"), GetItem);
	Button(Items, LOCTEXT("Money", "Money"), Say(TEXT("get money")));
	Button(Items, LOCTEXT("Weapons", "Weapons"), Say(TEXT("get weapons")));
	Button(Items, LOCTEXT("Armor", "Armor"), Say(TEXT("get armor")));
	Button(Items, LOCTEXT("Reagents", "Reagents"), Say(TEXT("get reagents")));
	Button(Items, LOCTEXT("Potions", "Potions"), Say(TEXT("get potions")));
	Button(Items, LOCTEXT("Scrolls", "Scrolls"), Say(TEXT("get scrolls")));
	Items->AddSlot()
	[
		SNew(SMRTextButton, Ui).TextSize(8.5f).MinWidth(38.f)
			.Text_Lambda([Weak]() { const TSharedPtr<SMRAdminConsole> D = Weak.Pin(); return D && D->bConfirmClear ? LOCTEXT("ClearSure", "Really clear?") : LOCTEXT("ClearInv", "Clear inventory"); })
			.IsEnabled_Lambda([WeakNet]() { return WeakNet.IsValid() && WeakNet->IsStaff(); })
			.OnClicked(FSimpleDelegate::CreateLambda([Weak]()
			{
				if (const TSharedPtr<SMRAdminConsole> D = Weak.Pin())
				{
					if (D->bConfirmClear)
					{
						D->DM(TEXT("clear inventory"));  // every deletable item: pressed twice
					}
					D->bConfirmClear = !D->bConfirmClear;
				}
			}))
	];
	AddRow(LOCTEXT("Items", "Items"), Items);

	TSharedRef<SWrapBox> Look = Wrap();
	TFunction<void()> Disguise = Field(DisguiseField, LOCTEXT("DisguiseHint", "A monster (\"orc\")"), TEXT("disguise"));
	Look->AddSlot()[DisguiseField.ToSharedRef()];
	Button(Look, LOCTEXT("Disguise", "Disguise"), Disguise);
	Button(Look, LOCTEXT("Reset", "Reset data"), [WeakNet]() { if (WeakNet.IsValid()) WeakNet->ReloadData(); });
	AddRow(LOCTEXT("Look", "Look"), Look);
	return Box;
}

TSharedRef<SWidget> SMRAdminConsole::MakeWorld()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRAdminConsole> Weak = SharedThis(this);
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(Net());
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Wrap = [&]() { return SNew(SWrapBox).PreferredSize(RowWidthPx * Px).InnerSlotPadding(FVector2D(2.f * Px, 2.f * Px)); };
	auto Button = [&](const TSharedRef<SWrapBox>& To, const FText& Text, TFunction<void()> Do, ENeeds Needs = ENeeds::Staff)
	{
		To->AddSlot()
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(8.5f).MinWidth(38.f)
				.IsEnabled_Lambda([WeakNet, Needs]() { return WeakNet.IsValid() && (Needs == ENeeds::Admin ? WeakNet->IsAdmin() : WeakNet->IsStaff()); })
				.OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(Do)))
		];
	};
	auto Say = [Weak](const TCHAR* Text) { FString T = Text; return [Weak, T]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->DM(T); }; };
	auto Run = [Weak](const TCHAR* Text) { FString T = Text; return [Weak, T]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->Admin(T); }; };
	auto AddRow = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[Row(S, Label, W)]; };
	/** A text field whose text follows Command when its button (or Enter) sends it. */
	auto Field = [&](TSharedPtr<SMRTextField>& Out, const FText& Hint, const FString& Command, float Width = 130.f)
	{
		TFunction<void()> Do = [Weak, &Out, Command]()
		{
			const TSharedPtr<SMRAdminConsole> D = Weak.Pin();
			if (D && Out.IsValid() && !Out->GetText().TrimStartAndEnd().IsEmpty())
			{
				D->DM(Command + TEXT(" ") + Out->GetText().TrimStartAndEnd());
			}
		};
		SAssignNew(Out, SMRTextField, Ui).Width(Width).MaxLength(200).HintText(Hint).OnSubmit(FSimpleDelegate::CreateLambda(Do));
		return Do;
	};

	// dm.kod: the time of day for everyone, until restored
	TSharedRef<SWrapBox> Time = Wrap();
	Button(Time, LOCTEXT("Morning", "Morning"), Say(TEXT("morning")));
	Button(Time, LOCTEXT("Afternoon", "Afternoon"), Say(TEXT("afternoon")));
	Button(Time, LOCTEXT("Evening", "Evening"), Say(TEXT("evening")));
	Button(Time, LOCTEXT("Night", "Night"), Say(TEXT("night")));
	Button(Time, LOCTEXT("Restore", "Real time"), Say(TEXT("restore time")));
	AddRow(LOCTEXT("Time", "Time"), Time);

	TSharedRef<SWrapBox> Mobs = Wrap();
	TFunction<void()> MakeMonster = Field(MonsterField, LOCTEXT("MonsterHint", "A monster (\"giant rat\")"), TEXT("monster"));
	Mobs->AddSlot()[MonsterField.ToSharedRef()];
	Button(Mobs, LOCTEXT("Make", "Make"), MakeMonster);
	Button(Mobs, LOCTEXT("Call", "Call one"), Say(TEXT("call monster")));
	Button(Mobs, LOCTEXT("Rumble", "Rumble"), Say(TEXT("rumble")));
	AddRow(LOCTEXT("Monsters", "Monsters"), Mobs);

	TSharedRef<SWrapBox> Place = Wrap();
	TFunction<void()> PlaceIt = Field(PlaceField, LOCTEXT("PlaceHint", "Scenery or a light (\"brazier\")"), TEXT("place"));
	Place->AddSlot()[PlaceField.ToSharedRef()];
	Button(Place, LOCTEXT("Place", "Place"), PlaceIt);
	Button(Place, LOCTEXT("Lights", "Lights?"), Say(TEXT("help lights")));
	Button(Place, LOCTEXT("Scenery", "Scenery?"), Say(TEXT("help scenery")));
	Button(Place, LOCTEXT("Sign", "Event sign"), Say(TEXT("event sign")));
	AddRow(LOCTEXT("Place", "Place"), Place);

	// dm.kod: "systemmessage" to everyone with the [###] prefix, "lemote" to this room
	SAssignNew(MessageField, SMRTextField, Ui).Width(150.f).MaxLength(200).HintText(LOCTEXT("MessageHint", "A message"));
	auto Message = [Weak](const TCHAR* Command)
	{
		const FString Cmd = Command;
		return [Weak, Cmd]()
		{
			const TSharedPtr<SMRAdminConsole> D = Weak.Pin();
			if (D && D->MessageField.IsValid() && !D->MessageField->GetText().TrimStartAndEnd().IsEmpty())
			{
				D->DM(Cmd + TEXT(" ") + D->MessageField->GetText().TrimStartAndEnd());
				D->MessageField->SetText(FString());
			}
		};
	};
	TSharedRef<SWrapBox> Tell = Wrap();
	Tell->AddSlot()[MessageField.ToSharedRef()];
	Button(Tell, LOCTEXT("Everyone", "To everyone"), Message(TEXT("systemmessage")));
	Button(Tell, LOCTEXT("ThisRoom", "To this room"), Message(TEXT("lemote")));
	AddRow(LOCTEXT("Message", "Message"), Tell);

	// the server itself: the maintenance port's commands (admins only)
	TSharedRef<SWrapBox> Server = Wrap();
	Button(Server, LOCTEXT("Who", "Who"), Run(TEXT("who")), ENeeds::Admin);
	Button(Server, LOCTEXT("Status", "Status"), Run(TEXT("show status")), ENeeds::Admin);
	Button(Server, LOCTEXT("Clock", "Clock"), Run(TEXT("show clock")), ENeeds::Admin);
	Button(Server, LOCTEXT("Save", "Save game"), Run(TEXT("save game")), ENeeds::Admin);
	Button(Server, LOCTEXT("AdminHelp", "Server help"), Run(TEXT("help")), ENeeds::Admin);
	Button(Server, LOCTEXT("DMHelp", "DM help"), Say(TEXT("help")));
	AddRow(LOCTEXT("Server", "Server"), Server);
	return Box;
}

// ------------------------------------------------------------------------------ the window

void SMRAdminConsole::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRAdminConsole> Weak = SharedThis(this);
	const UMRNetSubsystem* N = Net();

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (const TCHAR* T : {TEXT("Travel"), TEXT("Players"), TEXT("Self"), TEXT("World")})
	{
		const FString Name = T;
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(Name)).TextSize(9.f).MinWidth(44.f).bActive(Tab == Name)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak, Name]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->SetTab(Name); }))
		];
	}
	const TSharedRef<SWidget> Page = Tab == TEXT("Players") ? MakePlayers() : Tab == TEXT("Self") ? MakeSelf()
		: Tab == TEXT("World") ? MakeWorld() : MakeTravel();

	// the server's answers (BP_ADMIN, and the "dm" commands' messages): selectable, to copy object numbers
	OutputStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), S->Font(8.f).Size))
		.SetColorAndOpacity(S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)));
	TSharedRef<SScrollBar> Bar = SNew(SScrollBar).Thickness(FVector2D(3.f * Px, 3.f * Px));
	SAssignNew(Output, SMultiLineEditableText)
		.TextStyle(&OutputStyle)
		.IsReadOnly(true)
		.AutoWrapText(true)
		.AllowContextMenu(true)
		.VScrollBar(Bar);
	const TSharedRef<SWidget> Answers = SNew(SMRPanel, Ui).Background(NAME_None).Frame(TEXT("inset")).Padding(1.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(S->White()).ColorAndOpacity(FLinearColor::Black)]
			+ SOverlay::Slot()
			[
				SNew(SBox).HeightOverride(78.f * Px).Padding(2.f * Px, 1.f * Px)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[Output.ToSharedRef()]
					+ SHorizontalBox::Slot().AutoWidth()[Bar]
				]
			]
		];

	// the command line keeps what was typed across a rebuild (a tab change)
	const FString Typed = CommandField.IsValid() ? CommandField->GetText() : FString();
	SAssignNew(CommandField, SMRTextField, Ui).Width(WidthPx - 60.f).MaxLength(250)
		.HintText(N && N->IsAdmin() ? LOCTEXT("CmdHint", "A server command (\"help\"), or \"dm\" and a DM command") : LOCTEXT("CmdHintDM", "\"dm\" and a DM command (\"dm help\")"))
		.InitialText(FText::FromString(Typed))
		.OnSubmit(FSimpleDelegate::CreateLambda([Weak]()
		{
			if (const TSharedPtr<SMRAdminConsole> D = Weak.Pin(); D && D->CommandField.IsValid())
			{
				const FString Line = D->CommandField->GetText();
				D->CommandField->SetText(FString());
				D->RunLine(Line);
				if (D->CommandField.IsValid())
				{
					D->CommandField->Focus();
				}
			}
		}))
		.OnCancel(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->Close(); }));
	TSharedRef<SHorizontalBox> Command = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[CommandField.ToSharedRef()]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Send", "Send")).TextSize(9.f).MinWidth(40.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]()
				{
					if (const TSharedPtr<SMRAdminConsole> D = Weak.Pin(); D && D->CommandField.IsValid())
					{
						const FString Line = D->CommandField->GetText();
						D->CommandField->SetText(FString());
						D->RunLine(Line);
					}
				}))
		];

	TSharedRef<SHorizontalBox> Bottom = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			MRUI::Label(S, TAttribute<FText>::CreateLambda([Weak]()
			{
				const TSharedPtr<SMRAdminConsole> D = Weak.Pin();
				if (D && !D->Status.IsEmpty())
				{
					return FText::FromString(D->Status);
				}
				return LOCTEXT("Careful", "Everything here changes the live world at once.");
			}), 8.5f, false, Warn)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Clear", "Clear")).TextSize(9.f).MinWidth(40.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->RunLine(TEXT("clear")); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Close", "Close")).TextSize(9.f).MinWidth(44.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRAdminConsole> D = Weak.Pin()) D->Close(); }))
		];

	const FText Title = N && N->IsAdmin() ? LOCTEXT("TitleAdmin", "Admin Console") : LOCTEXT("TitleDM", "Admin Console (DM)");
	SetFrame(Title,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f * Px)[Tabs]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).MinDesiredHeight(122.f * Px)[Page]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Answers]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Command]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Bottom],
		WidthPx);
	OnAdminText();
}

#undef LOCTEXT_NAMESPACE
