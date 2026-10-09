#include "UI/SMRSocial.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRChatCommands.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRResources.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRControls.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRSocial"

namespace
{
	const FLinearColor Dim(0.72f, 0.7f, 0.64f);
	const FLinearColor Good(0.6f, 0.95f, 0.6f);
	const FLinearColor Bad(1.f, 0.55f, 0.45f);

	TSharedRef<STextBlock> Wrapped(UMRUIStyle* S, const FText& Text, float Size, const FLinearColor& Color = FLinearColor(1.f, 0.93f, 0.7f))
	{
		TSharedRef<STextBlock> T = MRUI::Label(S, Text, Size, false, Color);
		T->SetAutoWrapText(true);
		return T;
	}

	/** A row of buttons, right-aligned. */
	TSharedRef<SHorizontalBox> ButtonRow()
	{
		return SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget];
	}

	void AddButton(const TSharedRef<SHorizontalBox>& Row, UMRUISubsystem* Ui, const FText& Text, TFunction<void()> Do,
		TAttribute<bool> Enabled = true, float Width = 44.f)
	{
		const float Px = Ui->GetStyle()->Px();
		Row->AddSlot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(9.f).MinWidth(Width).IsEnabled(Enabled)
				.OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(Do)))
		];
	}

	UMRNetWorldSubsystem* NetWorldOf(const UMRUISubsystem* Ui)
	{
		const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
		UMRNetWorldSubsystem* W = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
		return W && W->IsActive() ? W : nullptr;
	}
}

// ------------------------------------------------------------------------------ the frame

SMRSocialWindow::~SMRSocialWindow()
{
	if (UMRNetSubsystem* N = Net())
	{
		N->OnUsersChanged.Remove(UsersHandle);
		N->OnMailChanged.Remove(MailHandle);
		N->OnMailSent.Remove(MailSentHandle);
		N->OnNewsChanged.Remove(NewsHandle);
		N->OnGuildChanged.Remove(GuildHandle);
	}
}

void SMRSocialWindow::Init(UMRUISubsystem* InUI, EMRWindow InKind)
{
	UI = InUI;
	Kind = InKind;
}

UMRNetSubsystem* SMRSocialWindow::Net() const
{
	const APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr;
	const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
}

FString SMRSocialWindow::DateText(int64 UnixSeconds)
{
	if (UnixSeconds <= 0)
	{
		return FString();
	}
	const FTimespan Local = FDateTime::Now() - FDateTime::UtcNow();
	const FDateTime T = FDateTime::FromUnixTimestamp(UnixSeconds) + Local;
	static const TCHAR* Months[] = {TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"),
		TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec")};
	return FString::Printf(TEXT("%s %02d %s"), Months[FMath::Clamp(T.GetMonth(), 1, 12) - 1], T.GetDay(), *T.ToString(TEXT("%H:%M")));
}

void SMRSocialWindow::Close()
{
	if (UMRUISubsystem* Ui = UI.Get())
	{
		Ui->SetWindowOpen(Kind, false);
	}
}

FReply SMRSocialWindow::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geo, Event);
}

void SMRSocialWindow::SetFrame(const FText& Title, const TSharedRef<SWidget>& Content, float WidthPx)
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SMRPanel, UI.Get()).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SBox).WidthOverride(WidthPx * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
				[
					MRUI::Label(S, Title, 12.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight()[Content]
			]
		]
	];
}

// ------------------------------------------------------------------------------ who

void SMRWhoDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Who);
	if (UMRNetSubsystem* N = Net())
	{
		UsersHandle = N->OnUsersChanged.AddSP(this, &SMRWhoDialog::Rebuild);
	}
	Rebuild();
}

void SMRWhoDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* N = Net();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakObjectPtr<UMRUISubsystem> WeakUI(Ui);
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(N);
	TArray<FMRNetUser> Users;
	if (N)
	{
		N->GetUsers().GenerateValueArray(Users);
	}
	Users.Sort([](const FMRNetUser& A, const FMRNetUser& B) { return A.Name < B.Name; });
	const uint32 Self = N ? N->GetPlayer().Id : 0;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FMRNetUser& U : Users)
	{
		const FString Name = U.Name;
		const bool bIgnored = N && N->IsIgnored(Name);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MRUI::Label(S, FText::FromString(bIgnored ? Name + TEXT("  (ignored)") : Name), 9.5f, U.Id == Self,
					bIgnored ? Dim : FLinearColor(1.f, 0.93f, 0.7f))
			];
		if (U.Id != Self)
		{
			AddButton(Row, Ui, LOCTEXT("Tell", "Tell"), [WeakUI, Name]()
			{
				if (WeakUI.IsValid())
				{
					WeakUI->SetWindowOpen(EMRWindow::Who, false);
					WeakUI->OpenChatWith(FString::Printf(TEXT("tell \"%s\" "), *Name));
				}
			}, true, 30.f);
			AddButton(Row, Ui, bIgnored ? LOCTEXT("Unignore", "Unignore") : LOCTEXT("Ignore", "Ignore"), [WeakNet, Name, bIgnored]()
			{
				if (WeakNet.IsValid())
				{
					WeakNet->SetIgnored(Name, !bIgnored);
				}
			}, true, 44.f);
		}
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.5f * Px)[Row];
	}
	auto Toggle = [WeakNet](bool FMRSocial::* Field)
	{
		return [WeakNet, Field]()
		{
			if (WeakNet.IsValid())
			{
				FMRSocial& Soc = WeakNet->GetSocial();
				Soc.*Field = !(Soc.*Field);
				WeakNet->SaveSocial();
				WeakNet->OnUsersChanged.Broadcast();
			}
		};
	};
	const FMRSocial Soc = N ? N->GetSocial() : FMRSocial();
	TSharedRef<SHorizontalBox> Options = SNew(SHorizontalBox);
	auto AddToggle = [&](const FText& Text, bool bOn, TFunction<void()> Do)
	{
		Options->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(8.5f).MinWidth(40.f).bActive(bOn).OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(Do)))
		];
	};
	AddToggle(LOCTEXT("IgnoreAll", "Ignore everyone"), Soc.bIgnoreAll, Toggle(&FMRSocial::bIgnoreAll));
	AddToggle(LOCTEXT("NoBroadcast", "No broadcasts"), Soc.bNoBroadcast, Toggle(&FMRSocial::bNoBroadcast));
	AddToggle(LOCTEXT("Times", "Timestamps"), Soc.bTimestamps, Toggle(&FMRSocial::bTimestamps));
	TSharedRef<SHorizontalBox> Buttons = ButtonRow();
	AddButton(Buttons, Ui, LOCTEXT("Close", "Close"), [this]() { Close(); });
	SetFrame(FText::Format(LOCTEXT("WhoTitle", "Who is on ({0})"), Users.Num()),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(150.f * Px)[SNew(SScrollBox) + SScrollBox::Slot()[Rows]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Options]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Buttons],
		230.f);
}

// ------------------------------------------------------------------------------ mail

void SMRMailDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Mail);
	if (UMRNetSubsystem* N = Net())
	{
		MailHandle = N->OnMailChanged.AddSP(this, &SMRMailDialog::Rebuild);
		MailSentHandle = N->OnMailSent.AddSP(this, &SMRMailDialog::OnSent);
	}
	// (built when opened: OnOpened; SharedThis isn't ready inside Construct)
}

void SMRMailDialog::OnOpened()
{
	bComposing = false;
	Status.Reset();
	if (UMRNetSubsystem* N = Net())
	{
		Selected = N->GetMail().Num() - 1;  // the newest
		N->RequestMail();                   // new mail comes in (the original's mailbox button)
	}
	Rebuild();
}

void SMRMailDialog::Compose(const FString& To, const FString& Subject, const FString& Body)
{
	bComposing = true;
	Status.Reset();
	Rebuild();
	if (ToField.IsValid())
	{
		ToField->SetText(To);
		SubjectField->SetText(Subject);
		BodyBox->SetText(Body);
		(To.IsEmpty() ? ToField : SubjectField)->Focus();
	}
}

void SMRMailDialog::SendComposed()
{
	UMRNetSubsystem* N = Net();
	if (!N || !ToField.IsValid())
	{
		return;
	}
	Status = TEXT("Checking the names...");
	N->SendMail(MRChat::SplitNames(ToField->GetText()), SubjectField->GetText(), BodyBox->GetText());
}

void SMRMailDialog::OnSent(bool bOk, const FString& Why)
{
	if (bOk)
	{
		bComposing = false;
		Status = TEXT("Your mail was sent.");
		Rebuild();
	}
	else
	{
		Status = Why;  // the fields stay as typed
	}
}

void SMRMailDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* N = Net();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRMailDialog> Weak = SharedThis(this);
	if (bComposing)
	{
		// what was typed survives a rebuild (new mail arriving meanwhile)
		const FString To = ToField.IsValid() ? ToField->GetText() : FString();
		const FString Subject = SubjectField.IsValid() ? SubjectField->GetText() : FString();
		const FString Body = BodyBox.IsValid() ? BodyBox->GetText() : FString();
		SAssignNew(ToField, SMRTextField, Ui).Width(220.f).MaxLength(400).HintText(LOCTEXT("ToHint", "Names, separated by commas"))
			.InitialText(FText::FromString(To));
		SAssignNew(SubjectField, SMRTextField, Ui).Width(220.f).MaxLength(50).InitialText(FText::FromString(Subject));
		SAssignNew(BodyBox, SMRTextBox, Ui).Width(268.f).Height(110.f).MaxLength(3000).InitialText(FText::FromString(Body));
		TSharedRef<SHorizontalBox> Buttons = ButtonRow();
		AddButton(Buttons, Ui, LOCTEXT("Send", "Send"), [Weak]() { if (TSharedPtr<SMRMailDialog> D = Weak.Pin()) D->SendComposed(); },
			TAttribute<bool>::CreateLambda([Weak]() { const TSharedPtr<SMRMailDialog> D = Weak.Pin(); return D && D->Net() && !D->Net()->IsSendingMail(); }));
		AddButton(Buttons, Ui, LOCTEXT("Cancel", "Cancel"), [Weak]()
		{
			if (TSharedPtr<SMRMailDialog> D = Weak.Pin())
			{
				D->bComposing = false;
				D->Status.Reset();
				D->Rebuild();
			}
		});
		auto Field = [&](const FText& Label, const TSharedRef<SWidget>& W)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(46.f * Px)[MRUI::Label(S, Label, 9.5f)]]
				+ SHorizontalBox::Slot().FillWidth(1.f)[W];
		};
		SetFrame(LOCTEXT("ComposeTitle", "New mail"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f * Px)[Field(LOCTEXT("To", "To:"), ToField.ToSharedRef())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f * Px)[Field(LOCTEXT("Subject", "Subject:"), SubjectField.ToSharedRef())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[BodyBox.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)
			[
				MRUI::Label(S, TAttribute<FText>::CreateLambda([Weak]() { const TSharedPtr<SMRMailDialog> D = Weak.Pin(); return FText::FromString(D ? D->Status : FString()); }),
					9.f, false, Bad)
			]
			+ SVerticalBox::Slot().AutoHeight()[Buttons],
			280.f);
		return;
	}
	const TArray<FMRNetMail> Mail = N ? N->GetMail() : TArray<FMRNetMail>();
	Selected = Mail.IsValidIndex(Selected) ? Selected : Mail.Num() - 1;
	TArray<FText> Items;
	for (const FMRNetMail& M : Mail)
	{
		Items.Add(FText::FromString(FString::Printf(TEXT("%s  -  %s  (%s)"), *M.From, M.Subject.IsEmpty() ? TEXT("(no subject)") : *M.Subject, *DateText(M.Time))));
	}
	SAssignNew(List, SMRSelectList, Ui).Width(268.f).Height(70.f).TextSize(9.f)
		.OnSelected(SMRSelectList::FOnRow::CreateLambda([Weak](int32 Row) { if (TSharedPtr<SMRMailDialog> D = Weak.Pin()) { D->Selected = Row; D->Rebuild(); } }));
	List->SetItems(Items);
	List->SetSelected(Selected);
	TSharedRef<SVerticalBox> Reading = SNew(SVerticalBox);
	if (Mail.IsValidIndex(Selected))
	{
		const FMRNetMail& M = Mail[Selected];
		Reading->AddSlot().AutoHeight()[MRUI::Label(S, FText::FromString(TEXT("From: ") + M.From), 9.f, true)];
		Reading->AddSlot().AutoHeight()[Wrapped(S, FText::FromString(TEXT("To: ") + FString::Join(M.To, TEXT(", "))), 9.f, Dim)];
		Reading->AddSlot().AutoHeight()[MRUI::Label(S, FText::FromString(TEXT("Date: ") + DateText(M.Time)), 9.f, false, Dim)];
		Reading->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)[MRUI::Label(S, FText::FromString(TEXT("Subject: ") + M.Subject), 9.f, true)];
		Reading->AddSlot().AutoHeight()[Wrapped(S, FText::FromString(M.Body), 9.5f)];
	}
	else
	{
		Reading->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("NoMail", "You have no mail."), 9.5f, false, Dim)];
	}
	TSharedRef<SHorizontalBox> Buttons = ButtonRow();
	const bool bHave = Mail.IsValidIndex(Selected);
	const FMRNetMail Current = bHave ? Mail[Selected] : FMRNetMail();
	AddButton(Buttons, Ui, LOCTEXT("New", "New"), [Weak]() { if (TSharedPtr<SMRMailDialog> D = Weak.Pin()) D->Compose(); });
	AddButton(Buttons, Ui, LOCTEXT("Reply", "Reply"), [Weak, Current]()
	{
		if (TSharedPtr<SMRMailDialog> D = Weak.Pin())
		{
			const FString Re = Current.Subject.StartsWith(TEXT("Re:")) ? Current.Subject : TEXT("Re: ") + Current.Subject;
			D->Compose(Current.From, Re.Left(50));
		}
	}, bHave);
	AddButton(Buttons, Ui, LOCTEXT("Delete", "Delete"), [Weak]()
	{
		if (TSharedPtr<SMRMailDialog> D = Weak.Pin(); D && D->Net())
		{
			D->Net()->DeleteMail(D->Selected);
		}
	}, bHave);
	AddButton(Buttons, Ui, LOCTEXT("Check", "Get new"), [Weak]() { if (TSharedPtr<SMRMailDialog> D = Weak.Pin(); D && D->Net()) D->Net()->RequestMail(); });
	AddButton(Buttons, Ui, LOCTEXT("Close", "Close"), [Weak]() { if (TSharedPtr<SMRMailDialog> D = Weak.Pin()) D->Close(); });
	SetFrame(FText::Format(LOCTEXT("MailTitle", "Mail ({0})"), Mail.Num()),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[List.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px)
		[
			SNew(SBox).HeightOverride(120.f * Px)[SNew(SScrollBox) + SScrollBox::Slot()[Reading]]
		]
		+ SVerticalBox::Slot().AutoHeight()[MRUI::Label(S, FText::FromString(Status), 9.f, false, Good)]
		+ SVerticalBox::Slot().AutoHeight()[Buttons],
		280.f);
}

// ------------------------------------------------------------------------------ news

void SMRNewsDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::News);
	if (UMRNetSubsystem* N = Net())
	{
		NewsHandle = N->OnNewsChanged.AddSP(this, &SMRNewsDialog::Rebuild);
	}
	// (built when opened: OnOpened; SharedThis isn't ready inside Construct)
}

void SMRNewsDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* N = Net();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRNewsDialog> Weak = SharedThis(this);
	const FMRNetNews News = N ? N->GetNetWorld().News : FMRNetNews();
	const FText Title = FText::FromString(News.Board.Name.IsEmpty() ? TEXT("News") : News.Board.Name);
	if (bComposing)
	{
		SAssignNew(TitleField, SMRTextField, Ui).Width(220.f).MaxLength(50);
		SAssignNew(BodyBox, SMRTextBox, Ui).Width(268.f).Height(120.f).MaxLength(3000);
		TSharedRef<SHorizontalBox> Buttons = ButtonRow();
		AddButton(Buttons, Ui, LOCTEXT("Post", "Post"), [Weak]()
		{
			TSharedPtr<SMRNewsDialog> D = Weak.Pin();
			if (D && D->Net() && !D->TitleField->GetText().TrimStartAndEnd().IsEmpty())
			{
				D->Net()->PostArticle(D->TitleField->GetText(), D->BodyBox->GetText());
				D->bComposing = false;
				D->Net()->RequestArticles();
				D->Rebuild();
			}
		});
		AddButton(Buttons, Ui, LOCTEXT("Cancel", "Cancel"), [Weak]() { if (TSharedPtr<SMRNewsDialog> D = Weak.Pin()) { D->bComposing = false; D->Rebuild(); } });
		SetFrame(Title,
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(46.f * Px)[MRUI::Label(S, LOCTEXT("Heading", "Title:"), 9.5f)]]
				+ SHorizontalBox::Slot().FillWidth(1.f)[TitleField.ToSharedRef()]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[BodyBox.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight()[Buttons],
			280.f);
		TitleField->Focus();
		return;
	}
	TArray<FText> Items;
	int32 Reading = INDEX_NONE;
	for (int32 i = 0; i < News.Articles.Num(); ++i)
	{
		const FMRNetArticle& A = News.Articles[i];
		Items.Add(FText::FromString(FString::Printf(TEXT("%s  -  %s, %s"), *A.Title, *A.Poster, *DateText(A.Time))));
		if (A.Num == News.ReadingNum && News.bHaveText)
		{
			Reading = i;
		}
	}
	TArray<uint32> Nums;
	for (const FMRNetArticle& A : News.Articles)
	{
		Nums.Add(A.Num);
	}
	SAssignNew(List, SMRSelectList, Ui).Width(268.f).Height(80.f).TextSize(9.f)
		.OnSelected(SMRSelectList::FOnRow::CreateLambda([Weak, Nums](int32 Row)
		{
			if (TSharedPtr<SMRNewsDialog> D = Weak.Pin(); D && D->Net() && Nums.IsValidIndex(Row))
			{
				D->Net()->ReadArticle(Nums[Row]);
			}
		}));
	List->SetItems(Items);
	List->SetSelected(Reading);
	const FText Body = !News.bHaveArticles ? LOCTEXT("Loading", "Reading the board...")
		: News.Articles.IsEmpty() ? LOCTEXT("Empty", "Nothing has been posted here.")
		: News.bHaveText ? FText::FromString(News.ReadingText) : LOCTEXT("Pick", "Choose an article to read it.");
	TSharedRef<SHorizontalBox> Buttons = ButtonRow();
	if (News.Permission & MRMsg::NEWS_POST)
	{
		AddButton(Buttons, Ui, LOCTEXT("New", "Post"), [Weak]() { if (TSharedPtr<SMRNewsDialog> D = Weak.Pin()) { D->bComposing = true; D->Rebuild(); } });
	}
	AddButton(Buttons, Ui, LOCTEXT("Refresh", "Refresh"), [Weak]() { if (TSharedPtr<SMRNewsDialog> D = Weak.Pin(); D && D->Net()) D->Net()->RequestArticles(); });
	AddButton(Buttons, Ui, LOCTEXT("Close", "Close"), [Weak]() { if (TSharedPtr<SMRNewsDialog> D = Weak.Pin()) D->Close(); });
	SetFrame(Title,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Wrapped(S, FText::FromString(MRServerText::StripStyle(News.Description)), 8.5f, Dim)]
		+ SVerticalBox::Slot().AutoHeight()[List.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px)
		[
			SNew(SBox).HeightOverride(120.f * Px)[SNew(SScrollBox) + SScrollBox::Slot()[Wrapped(S, Body, 9.5f)]]
		]
		+ SVerticalBox::Slot().AutoHeight()[Buttons],
		280.f);
}

// ------------------------------------------------------------------------------ guild

void SMRGuildDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Guild);
	if (UMRNetSubsystem* N = Net())
	{
		GuildHandle = N->OnGuildChanged.AddSP(this, &SMRGuildDialog::Rebuild);
	}
	// (built when opened: OnOpened; SharedThis isn't ready inside Construct)
}

TSharedRef<SWidget> SMRGuildDialog::MakeMembers()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	const FMRNetGuild& G = N->GetNetWorld().Guild;
	TWeakPtr<SMRGuildDialog> Weak = SharedThis(this);
	TArray<FMRNetGuildMember> Members = G.Members;
	Members.Sort([](const FMRNetGuildMember& A, const FMRNetGuildMember& B) { return A.Rank != B.Rank ? A.Rank > B.Rank : A.Name < B.Name; });
	auto RankName = [&G](const FMRNetGuildMember& M)
	{
		const int32 R = FMath::Clamp<int32>(M.Rank, 1, MRMsg::GuildRanks) - 1;
		return M.Gender == 2 ? G.FemaleRanks[R] : G.MaleRanks[R];
	};
	TArray<FText> Items;
	for (const FMRNetGuildMember& M : Members)
	{
		Items.Add(FText::FromString(FString::Printf(TEXT("%s  -  %s%s"), *M.Name, *RankName(M), M.Id == G.CurrentVote ? TEXT("  (your vote)") : TEXT(""))));
	}
	SelectedMember = Members.IsValidIndex(SelectedMember) ? SelectedMember : INDEX_NONE;
	TSharedRef<SMRSelectList> List = SNew(SMRSelectList, Ui).Width(268.f).Height(110.f).TextSize(9.f)
		.OnSelected(SMRSelectList::FOnRow::CreateLambda([Weak](int32 Row) { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) { D->SelectedMember = Row; D->Rebuild(); } }));
	List->SetItems(Items);
	List->SetSelected(SelectedMember);
	const FMRNetGuildMember Sel = Members.IsValidIndex(SelectedMember) ? Members[SelectedMember] : FMRNetGuildMember();
	const uint32 Self = N->GetPlayer().Id;
	const bool bOther = Sel.Id != 0 && Sel.Id != Self;
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(N);
	auto Cmd = [WeakNet](uint8 C, uint32 Id)
	{
		return [WeakNet, C, Id]()
		{
			if (WeakNet.IsValid())
			{
				WeakNet->GuildCommand(C, Id);
				WeakNet->GuildCommand(MRMsg::UC_REQ_GUILDINFO);  // the answer shows the change
			}
		};
	};
	TSharedRef<SHorizontalBox> Row1 = ButtonRow();
	AddButton(Row1, Ui, LOCTEXT("Vote", "Vote for"), Cmd(MRMsg::UC_VOTE, Sel.Id), Sel.Id != 0 && (G.Flags & MRMsg::GC_VOTE));
	AddButton(Row1, Ui, LOCTEXT("Up", "Raise"), [WeakNet, Sel]()
	{
		if (WeakNet.IsValid())
		{
			WeakNet->GuildSetRank(Sel.Id, Sel.Rank + 1);
			WeakNet->GuildCommand(MRMsg::UC_REQ_GUILDINFO);
		}
	}, bOther && (G.Flags & MRMsg::GC_SET_RANK) && Sel.Rank < MRMsg::GuildRanks - 1);
	AddButton(Row1, Ui, LOCTEXT("Down", "Lower"), [WeakNet, Sel]()
	{
		if (WeakNet.IsValid())
		{
			WeakNet->GuildSetRank(Sel.Id, Sel.Rank - 1);
			WeakNet->GuildCommand(MRMsg::UC_REQ_GUILDINFO);
		}
	}, bOther && (G.Flags & MRMsg::GC_SET_RANK) && Sel.Rank > 1);
	AddButton(Row1, Ui, LOCTEXT("Exile", "Exile"), Cmd(MRMsg::UC_EXILE, Sel.Id), bOther && (G.Flags & MRMsg::GC_EXILE));
	AddButton(Row1, Ui, LOCTEXT("Abdicate", "Abdicate to"), Cmd(MRMsg::UC_ABDICATE, Sel.Id), bOther && (G.Flags & MRMsg::GC_ABDICATE), 54.f);
	const UMRNetWorldSubsystem* World = NetWorldOf(Ui);
	const uint32 Target = World ? World->GetTargetId() : 0;
	TSharedRef<SHorizontalBox> Row2 = ButtonRow();
	AddButton(Row2, Ui, LOCTEXT("Invite", "Invite target"), Cmd(MRMsg::UC_INVITE, Target), Target != 0 && Target != Self && (G.Flags & MRMsg::GC_INVITE), 60.f);
	AddButton(Row2, Ui, LOCTEXT("Renounce", "Renounce"), Cmd(MRMsg::UC_RENOUNCE, 0), (G.Flags & MRMsg::GC_RENOUNCE) != 0);
	AddButton(Row2, Ui, LOCTEXT("Disband", "Disband"), Cmd(MRMsg::UC_DISBAND, 0), (G.Flags & MRMsg::GC_DISBAND) != 0);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[List]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[Row1]
		+ SVerticalBox::Slot().AutoHeight()[Row2];
}

TSharedRef<SWidget> SMRGuildDialog::MakeGuilds()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	const FMRNetGuild& G = N->GetNetWorld().Guild;
	const FMRNetGuildList& L = N->GetNetWorld().GuildList;
	TWeakPtr<SMRGuildDialog> Weak = SharedThis(this);
	TArray<TPair<uint32, FString>> Guilds;
	for (const TPair<uint32, FString>& P : L.Guilds)
	{
		if (P.Key != G.GuildId)
		{
			Guilds.Add(P);
		}
	}
	TArray<FText> Items;
	for (const TPair<uint32, FString>& P : Guilds)
	{
		TArray<FString> Marks;
		if (L.Allies.Contains(P.Key)) Marks.Add(TEXT("ally"));
		if (L.Enemies.Contains(P.Key)) Marks.Add(TEXT("enemy"));
		if (L.DeclaredAllies.Contains(P.Key)) Marks.Add(TEXT("calls us ally"));
		if (L.DeclaredEnemies.Contains(P.Key)) Marks.Add(TEXT("calls us enemy"));
		Items.Add(FText::FromString(Marks.IsEmpty() ? P.Value : FString::Printf(TEXT("%s  (%s)"), *P.Value, *FString::Join(Marks, TEXT(", ")))));
	}
	SelectedGuild = Guilds.IsValidIndex(SelectedGuild) ? SelectedGuild : INDEX_NONE;
	TSharedRef<SMRSelectList> List = SNew(SMRSelectList, Ui).Width(268.f).Height(110.f).TextSize(9.f)
		.OnSelected(SMRSelectList::FOnRow::CreateLambda([Weak](int32 Row) { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) { D->SelectedGuild = Row; D->Rebuild(); } }));
	List->SetItems(Items);
	List->SetSelected(SelectedGuild);
	const uint32 Id = Guilds.IsValidIndex(SelectedGuild) ? Guilds[SelectedGuild].Key : 0;
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(N);
	auto Cmd = [WeakNet, Id](uint8 C)
	{
		return [WeakNet, Id, C]()
		{
			if (WeakNet.IsValid())
			{
				WeakNet->GuildCommand(C, Id);
				WeakNet->GuildCommand(MRMsg::UC_REQ_GUILD_LIST);
			}
		};
	};
	TSharedRef<SHorizontalBox> Row = ButtonRow();
	AddButton(Row, Ui, LOCTEXT("Ally", "Ally"), Cmd(MRMsg::UC_MAKE_ALLIANCE), Id && !L.Allies.Contains(Id) && (G.Flags & MRMsg::GC_MAKE_ALLIANCE));
	AddButton(Row, Ui, LOCTEXT("EndAlly", "End alliance"), Cmd(MRMsg::UC_END_ALLIANCE), Id && L.Allies.Contains(Id) && (G.Flags & MRMsg::GC_END_ALLIANCE), 56.f);
	AddButton(Row, Ui, LOCTEXT("Enemy", "Enemy"), Cmd(MRMsg::UC_MAKE_ENEMY), Id && !L.Enemies.Contains(Id) && (G.Flags & MRMsg::GC_DECLARE_ENEMY));
	AddButton(Row, Ui, LOCTEXT("Peace", "Make peace"), Cmd(MRMsg::UC_END_ENEMY), Id && L.Enemies.Contains(Id) && (G.Flags & MRMsg::GC_END_ENEMY), 54.f);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[L.bValid ? StaticCastSharedRef<SWidget>(List) : StaticCastSharedRef<SWidget>(MRUI::Label(S, LOCTEXT("NoList", "Asking for the guild list..."), 9.5f, false, Dim))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)[Row];
}

TSharedRef<SWidget> SMRGuildDialog::MakeCreate()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	TWeakPtr<SMRGuildDialog> Weak = SharedThis(this);
	SAssignNew(NameField, SMRTextField, Ui).Width(200.f).MaxLength(30);
	RankFields.Reset();
	TSharedRef<SVerticalBox> Ranks = SNew(SVerticalBox);
	const TCHAR* RankHints[MRMsg::GuildRanks] = {TEXT("Lowest rank"), TEXT("Second rank"), TEXT("Third rank"), TEXT("Fourth rank"), TEXT("Guildmaster")};
	for (int32 i = 0; i < MRMsg::GuildRanks; ++i)
	{
		TSharedPtr<SMRTextField> Male, Female;
		SAssignNew(Male, SMRTextField, Ui).Width(120.f).MaxLength(20).HintText(FText::FromString(FString(RankHints[i]) + TEXT(" (men)")));
		SAssignNew(Female, SMRTextField, Ui).Width(120.f).MaxLength(20).HintText(FText::FromString(FString(RankHints[i]) + TEXT(" (women)")));
		RankFields.Add(Male);
		RankFields.Add(Female);
		Ranks->AddSlot().AutoHeight().Padding(0.f, 0.5f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f * Px, 0.f)[Male.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth()[Female.ToSharedRef()]
		];
	}
	const int32 Cost = N->GetNetWorld().GuildCost, Secret = N->GetNetWorld().GuildSecretCost;
	TSharedRef<SHorizontalBox> Buttons = ButtonRow();
	Buttons->InsertSlot(0).AutoWidth()
	[
		SNew(SMRTextButton, Ui).Text(FText::Format(LOCTEXT("Secret", "Secret guild ({0} more)"), FText::AsNumber(Secret - Cost))).TextSize(8.5f)
			.bActive_Lambda([Weak]() { const TSharedPtr<SMRGuildDialog> D = Weak.Pin(); return D && D->bSecret; })
			.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) D->bSecret = !D->bSecret; }))
	];
	AddButton(Buttons, Ui, LOCTEXT("Found", "Found it"), [Weak]()
	{
		TSharedPtr<SMRGuildDialog> D = Weak.Pin();
		if (!D || !D->Net() || D->NameField->GetText().TrimStartAndEnd().IsEmpty())
		{
			return;
		}
		TArray<FString> Names;
		for (const TSharedPtr<SMRTextField>& F : D->RankFields)
		{
			Names.Add(F->GetText().TrimStartAndEnd());
		}
		D->Net()->GuildCreate(D->NameField->GetText().TrimStartAndEnd(), Names, D->bSecret);
		D->Close();
	});
	AddButton(Buttons, Ui, LOCTEXT("Cancel", "Cancel"), [Weak]() { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) D->Close(); });
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Wrapped(S, FText::Format(LOCTEXT("CreateHelp", "Found a guild for {0} shillings. Name it and its five ranks, lowest first."), FText::AsNumber(Cost)), 9.f, Dim)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(46.f * Px)[MRUI::Label(S, LOCTEXT("Name", "Name:"), 9.5f)]]
			+ SHorizontalBox::Slot().FillWidth(1.f)[NameField.ToSharedRef()]
		]
		+ SVerticalBox::Slot().AutoHeight()[Ranks]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Buttons];
}

void SMRGuildDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* N = Net();
	if (!Ui || !Ui->GetStyle() || !N)
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const FMRNetWorld& W = N->GetNetWorld();
	TWeakPtr<SMRGuildDialog> Weak = SharedThis(this);
	if (!W.Guild.bValid && W.GuildCost > 0)
	{
		SetFrame(LOCTEXT("CreateTitle", "Found a guild"), MakeCreate(), 270.f);
		return;
	}
	if (!W.Guild.bValid)
	{
		TSharedRef<SHorizontalBox> Buttons = ButtonRow();
		AddButton(Buttons, Ui, LOCTEXT("Close", "Close"), [Weak]() { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) D->Close(); });
		SetFrame(LOCTEXT("GuildTitle", "Guild"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Wrapped(S, LOCTEXT("NoGuild", "You are not in a guild. A guild creator (in Barloque) founds one; a guild's members can invite you."), 9.5f, Dim)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Buttons],
			230.f);
		return;
	}
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	auto AddTab = [&](const FText& Text, bool bGuilds)
	{
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(9.f).MinWidth(60.f).bActive(bShowGuilds == bGuilds)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak, bGuilds]()
				{
					if (TSharedPtr<SMRGuildDialog> D = Weak.Pin())
					{
						D->bShowGuilds = bGuilds;
						if (bGuilds && D->Net())
						{
							D->Net()->GuildCommand(MRMsg::UC_REQ_GUILD_LIST);
						}
						D->Rebuild();
					}
				}))
		];
	};
	AddTab(FText::Format(LOCTEXT("Members", "Members ({0})"), W.Guild.Members.Num()), false);
	AddTab(LOCTEXT("Guilds", "Other guilds"), true);
	TSharedRef<SHorizontalBox> Buttons = ButtonRow();
	AddButton(Buttons, Ui, LOCTEXT("Refresh", "Refresh"), [Weak]() { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin(); D && D->Net()) D->Net()->GuildCommand(MRMsg::UC_REQ_GUILDINFO); });
	AddButton(Buttons, Ui, LOCTEXT("Close", "Close"), [Weak]() { if (TSharedPtr<SMRGuildDialog> D = Weak.Pin()) D->Close(); });
	SetFrame(FText::FromString(W.Guild.Name),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Tabs]
		+ SVerticalBox::Slot().AutoHeight()[bShowGuilds ? MakeGuilds() : MakeMembers()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Buttons],
		280.f);
}

#undef LOCTEXT_NAMESPACE
