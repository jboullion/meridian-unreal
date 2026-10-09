#include "UI/SMRLoginScreen.h"

#include "Engine/GameInstance.h"
#include "GeneralProjectSettings.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRCharCreator.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRLogin"

namespace
{
	constexpr float WidthPx = 210.f;      // the window's inside, original pixels
	constexpr float PageHeightPx = 132.f;
	constexpr float CharListPx = 120.f;   // the character page: the list's column...
	constexpr float MotdPx = 190.f;       // ...and the message of the day's, to its right (the window widens)

	using MRUI::Label;

	FLinearColor Dim() { return FLinearColor(0.78f, 0.75f, 0.68f); }
}

SMRLoginScreen::~SMRLoginScreen()
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->OnCharactersChanged.Remove(CharactersHandle);
		Net->OnPhaseChanged.Remove(PhaseHandle);
	}
}

UMRNetSubsystem* SMRLoginScreen::GetNet() const
{
	const ULocalPlayer* LP = UI.IsValid() ? UI->GetLocalPlayer() : nullptr;
	const UGameInstance* GI = LP ? LP->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
}

void SMRLoginScreen::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	if (UMRNetSubsystem* Net = GetNet())
	{
		Server = Net->GetLastServer();
		CharactersHandle = Net->OnCharactersChanged.AddSP(this, &SMRLoginScreen::RebuildCharacters);
		PhaseHandle = Net->OnPhaseChanged.AddLambda([this]()
		{
			LocalError.Reset();
			RebuildCharacters();
			OnShown();
		});
	}

	TSharedRef<SOverlay> Pages = SNew(SOverlay);
	const TSharedRef<SWidget> PageWidgets[] = {MakeLoginPage(), MakeConnectingPage(), MakeCharactersPage()};
	for (int32 i = 0; i < UE_ARRAY_COUNT(PageWidgets); ++i)
	{
		const EPage P = static_cast<EPage>(i);
		Pages->AddSlot()
		[
			SNew(SBox).Visibility_Lambda([this, P]() { return PageVisibility(P); })
			[
				PageWidgets[i]
			]
		];
	}

	auto Error = TAttribute<FText>::CreateLambda([this]()
	{
		const UMRNetSubsystem* Net = GetNet();
		const FString E = !LocalError.IsEmpty() ? LocalError : (Net ? Net->GetLastError() : FString());
		return FText::FromString(E);
	});
	TSharedRef<STextBlock> ErrorText = Label(S, Error, 9.f, true, FLinearColor(1.f, 0.42f, 0.3f));
	ErrorText->SetAutoWrapText(true);
	ErrorText->SetJustification(ETextJustify::Center);

	ChildSlot
	[
		SNew(SOverlay)
		// the creator replaces the window while a character is made
		+ SOverlay::Slot()
		[
			SAssignNew(Creator, SMRCharCreator, InUI)
			.Visibility_Lambda([this]() { return GetPage() == EPage::Create ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
		SNew(SMRPanel, InUI).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		.Visibility_Lambda([this]() { return GetPage() == EPage::Create ? EVisibility::Collapsed : EVisibility::Visible; })
		[
			SNew(SBox).WidthOverride_Lambda([this, Px]()
			{
				return (GetPage() == EPage::Characters && HasMotd() ? CharListPx + MotdPx + 6.f : WidthPx) * Px;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 4.f * Px)
				[
					Label(S, LOCTEXT("Title", "Unreal Meridian"), 15.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(5.f)
					[
						SNew(SBox).HeightOverride(PageHeightPx * Px)
						[
							Pages
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)
				[
					ErrorText
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)
				[
					MakeButtons()
				]
			]
		]
		]
		// the release (ProjectVersion in DefaultGame.ini, set by tools/ue/release.ps1), for bug reports
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.f * Px, 4.f * Px)
		[
			Label(S, FText::FromString(TEXT("v") + GetDefault<UGeneralProjectSettings>()->ProjectVersion), 8.f, false, Dim())
		]
	];
	RebuildCharacters();
}

// ------------------------------------------------------------------------------ pages

TSharedRef<SWidget> SMRLoginScreen::MakeLoginPage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const UMRNetSubsystem* Net = GetNet();

	TSharedRef<SHorizontalBox> Servers = SNew(SHorizontalBox);
	if (Net)
	{
		for (int32 i = 0; i < Net->GetServers().Num(); ++i)
		{
			Servers->AddSlot().FillWidth(1.f).Padding(i > 0 ? 2.f * Px : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(FText::FromString(Net->GetServers()[i].Name)).TextSize(8.f)
					.bActive_Lambda([this, i]() { return Server == i; })
					.OnClicked_Lambda([this, i]() { Server = i; })
			];
		}
	}
	TSharedRef<STextBlock> Hint = Label(S, LOCTEXT("NewAccount",
		"New here? Enter the name and password you want: the account is made when you log in."), 8.f, false, Dim());
	Hint->SetAutoWrapText(true);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Label(S, LOCTEXT("Server", "Server"), 9.f, false, Dim())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 4.f * Px)[Servers]
		+ SVerticalBox::Slot().AutoHeight()[Label(S, LOCTEXT("LoginName", "Login name"), 9.f, false, Dim())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 3.f * Px)
		[
			SAssignNew(NameField, SMRTextField, Ui).Width(WidthPx - 18.f).MaxLength(30)
			.InitialText(FText::FromString(Net ? Net->GetLastUser() : FString()))
			.OnSubmit_Lambda([this]() { PasswordField->Focus(); })
		]
		+ SVerticalBox::Slot().AutoHeight()[Label(S, LOCTEXT("Password", "Password"), 9.f, false, Dim())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 4.f * Px)
		[
			SAssignNew(PasswordField, SMRTextField, Ui).Width(WidthPx - 18.f).MaxLength(40).bPassword(true)
			.OnSubmit(FSimpleDelegate::CreateSP(this, &SMRLoginScreen::LogIn))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint];
}

TSharedRef<SWidget> SMRLoginScreen::MakeConnectingPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	auto StatusText = TAttribute<FText>::CreateLambda([this]()
	{
		const UMRNetSubsystem* Net = GetNet();
		const FString St = Net ? Net->GetStatus() : FString();
		return St.IsEmpty() ? LOCTEXT("Wait", "Please wait...") : FText::FromString(St);
	});
	auto ServerName = TAttribute<FText>::CreateLambda([this]()
	{
		const UMRNetSubsystem* Net = GetNet();
		const FMRServerEntry* E = Net ? Net->GetServer() : nullptr;
		return E ? FText::FromString(E->Name) : FText::GetEmpty();
	});
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Label(S, StatusText, 12.f, true)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f * Px)[Label(S, ServerName, 9.f, false, Dim())]
		+ SVerticalBox::Slot().FillHeight(1.f);
}

TSharedRef<SWidget> SMRLoginScreen::MakeCharactersPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const FLinearColor Heading = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	TSharedRef<STextBlock> MotdText = Label(S, TAttribute<FText>::CreateLambda([this]() { return FText::FromString(GetMotd()); }), 8.f, false, Dim());
	MotdText->SetAutoWrapText(true);
	auto MotdVisibility = [this]() { return HasMotd() ? EVisibility::Visible : EVisibility::Collapsed; };
	// a groove in the stone between the columns: a dark line beside a light one
	auto Groove = [S, Px](const FLinearColor& C)
	{
		return SNew(SBox).WidthOverride(0.5f * Px)[SNew(SImage).Image(S->White()).ColorAndOpacity(C)];
	};
	// the list on the left and the message of the day on the right, so a long message doesn't squeeze the list
	return SNew(SHorizontalBox)
		// (proportional fills: a capped column's leftover width isn't given to the other one)
		+ SHorizontalBox::Slot().FillWidth(TAttribute<float>::CreateLambda([this]() { return HasMotd() ? CharListPx : 1.f; }))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)
			[
				Label(S, LOCTEXT("Choose", "Choose a character"), 10.f, true, Heading)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox).ScrollBarThickness(FVector2D(4.f * Px, 4.f * Px))
				+ SScrollBox::Slot()[SAssignNew(CharacterList, SVerticalBox)]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox).Visibility_Lambda(MotdVisibility)
			+ SHorizontalBox::Slot().AutoWidth()[Groove(FLinearColor(0.f, 0.f, 0.f, 0.55f))]
			+ SHorizontalBox::Slot().AutoWidth()[Groove(FLinearColor(1.f, 1.f, 1.f, 0.18f))]
		]
		+ SHorizontalBox::Slot().FillWidth(MotdPx).Padding(3.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SVerticalBox).Visibility_Lambda(MotdVisibility)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)
			[
				Label(S, LOCTEXT("Motd", "News"), 10.f, true, Heading)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox).ScrollBarThickness(FVector2D(4.f * Px, 4.f * Px))
				+ SScrollBox::Slot().Padding(0.f, 0.f, 3.f * Px, 0.f)[MotdText]
			]
		];
}

FString SMRLoginScreen::GetMotd() const
{
	const UMRNetSubsystem* Net = GetNet();
	const FString M = Net ? Net->GetMotd().TrimStartAndEnd() : FString();
	return M == TEXT("<Default>") ? FString() : M;
}

bool SMRLoginScreen::HasMotd() const
{
	return !GetMotd().IsEmpty();
}

TSharedRef<SWidget> SMRLoginScreen::MakeButtons()
{
	UMRUISubsystem* Ui = UI.Get();
	const float Px = Ui->GetStyle()->Px();
	auto PrimaryText = TAttribute<FText>::CreateLambda([this]()
	{
		switch (GetPage())
		{
		case EPage::Login: return LOCTEXT("LogIn", "Log In");
		case EPage::Connecting: return LOCTEXT("Cancel", "Cancel");
		case EPage::Characters:
			return Entries.IsValidIndex(Selected) && Entries[Selected].bEmpty ? LOCTEXT("Make", "Create...") : LOCTEXT("Play", "Play");
		default: return LOCTEXT("Cancel", "Cancel");
		}
	});
	auto SecondaryText = TAttribute<FText>::CreateLambda([this]()
	{
		switch (GetPage())
		{
		case EPage::Login: return LOCTEXT("Quit", "Quit");
		case EPage::Characters: return LOCTEXT("LogOff", "Log Off");
		default: return LOCTEXT("Back", "Back");
		}
	});
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SMRTextButton, Ui).Text(PrimaryText).TextSize(10.f)
				.IsEnabled_Lambda([this]()
				{
					const UMRNetSubsystem* Net = GetNet();
					return GetPage() != EPage::Characters || (Entries.IsValidIndex(Selected) && Net && Net->GetStatus().IsEmpty());
				})
				.OnClicked_Lambda([this]()
				{
					switch (GetPage())
					{
					case EPage::Login: LogIn(); break;
					case EPage::Connecting: LogOff(); break;
					case EPage::Characters: Play(); break;
					default: break;
					}
				})
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(3.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(SecondaryText).TextSize(10.f)
				.Visibility_Lambda([this]() { return GetPage() == EPage::Connecting ? EVisibility::Hidden : EVisibility::Visible; })
				.OnClicked_Lambda([this]()
				{
					switch (GetPage())
					{
					case EPage::Login: Quit(); break;
					case EPage::Characters: LogOff(); break;
					default: break;
					}
				})
		];
}

void SMRLoginScreen::RebuildCharacters()
{
	const UMRNetSubsystem* Net = GetNet();
	if (!CharacterList.IsValid() || !Net)
	{
		return;
	}
	Entries.Reset();
	TArray<FEntry> Empty;
	for (const FMRCharacterSlot& C : Net->GetCharacters())
	{
		FEntry E;
		E.Id = C.Id;
		E.bEmpty = C.bNeedsCreation;
		E.Name = C.bNeedsCreation ? TEXT("<New character>") : C.Name;
		(E.bEmpty ? Empty : Entries).Add(E);
	}
	Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0; });
	Entries.Append(Empty);
	Selected = FMath::Clamp(Selected, 0, FMath::Max(0, Entries.Num() - 1));

	UMRUISubsystem* Ui = UI.Get();
	const float Px = Ui->GetStyle()->Px();
	CharacterList->ClearChildren();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		CharacterList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f * Px)
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(Entries[i].Name)).TextSize(9.f)
				.bActive_Lambda([this, i]() { return Selected == i; })
				.OnClicked_Lambda([this, i]()
				{
					// a click on the selected one plays it (the original's double click)
					if (Selected == i)
					{
						Play();
					}
					Selected = i;
				})
		];
	}
}

EVisibility SMRLoginScreen::PageVisibility(EPage Page) const
{
	return GetPage() == Page ? EVisibility::Visible : EVisibility::Hidden;
}

SMRLoginScreen::EPage SMRLoginScreen::GetPage() const
{
	const UMRNetSubsystem* Net = GetNet();
	switch (Net ? Net->GetPhase() : EMRNetPhase::Offline)
	{
	case EMRNetPhase::Offline: return EPage::Login;
	case EMRNetPhase::Characters: return EPage::Characters;
	// the creator once the server's options are in (until then: "Asking the server...")
	case EMRNetPhase::Creating: return Net->GetCharInfo().IsValid() ? EPage::Create : EPage::Connecting;
	default: return EPage::Connecting;
	}
}

void SMRLoginScreen::OnShown()
{
	TSharedPtr<SMRTextField> Field;
	switch (GetPage())
	{
	case EPage::Login: Field = NameField.IsValid() && NameField->GetText().IsEmpty() ? NameField : PasswordField; break;
	case EPage::Create:
		if (Creator.IsValid())
		{
			Creator->OnShown();
		}
		return;
	default: break;
	}
	if (Field.IsValid())
	{
		Field->Focus();
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

// ------------------------------------------------------------------------------ actions

void SMRLoginScreen::LogIn()
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net || GetPage() != EPage::Login)
	{
		return;
	}
	const FString Name = NameField->GetText().TrimStartAndEnd();
	const FString Password = PasswordField->GetText();
	if (Name.IsEmpty() || Password.IsEmpty())
	{
		LocalError = TEXT("Enter your login name and password.");
		return;
	}
	LocalError.Reset();
	Net->Connect(Server, Name, Password);
	PasswordField->SetText(FString());  // never kept on screen
}

void SMRLoginScreen::Play()
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net || !Entries.IsValidIndex(Selected) || !Net->GetStatus().IsEmpty())
	{
		return;
	}
	const FEntry& E = Entries[Selected];
	LocalError.Reset();
	if (E.bEmpty)
	{
		Net->RequestCharInfo(E.Id);  // the creator opens when the server's options arrive
		return;
	}
	Net->UseCharacter(E.Id);
}

void SMRLoginScreen::LogOff()
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->Logoff();
	}
}

void SMRLoginScreen::Quit()
{
	if (APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr)
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}

// ------------------------------------------------------------------------------ input and drawing

FReply SMRLoginScreen::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Enter)
	{
		switch (GetPage())
		{
		case EPage::Login: LogIn(); break;
		case EPage::Characters: Play(); break;
		default: return FReply::Unhandled();
		}
		return FReply::Handled();
	}
	if ((Key == EKeys::Up || Key == EKeys::Down) && GetPage() == EPage::Characters)
	{
		Selected = FMath::Clamp(Selected + (Key == EKeys::Up ? -1 : 1), 0, FMath::Max(0, Entries.Num() - 1));
		return FReply::Handled();
	}
	if (Key == EKeys::Escape && GetPage() == EPage::Create && Creator.IsValid())
	{
		return Creator->OnKeyDown(Geo, Event);
	}
	return FReply::Unhandled();
}

int32 SMRLoginScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	// dim Raza behind the window, a little (the town is the backdrop)
	if (UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr)
	{
		MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()),
			S->Color(TEXT("dim"), FLinearColor(0.f, 0.f, 0.f, 0.35f)));
	}
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 1, WStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE
