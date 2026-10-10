#include "UI/SMRGameMenu.h"

#include "Framework/Application/SlateApplication.h"
#include "Net/MRNetSubsystem.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "MRGameMenu"

namespace
{
	constexpr float WidthPx = 120.f;  // the panel's inside, original pixels
}

void SMRGameMenu::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI ? InUI->GetStyle() : nullptr;
	if (!S)
	{
		return;
	}
	const float Px = S->Px();
	TWeakObjectPtr<UMRUISubsystem> Weak(InUI);
	auto Button = [InUI, Px](const FText& Text, TFunction<void()> OnClick, bool bEnabled = true)
	{
		return SNew(SBox).Padding(0.f, 2.f * Px)
			[
				SNew(SMRTextButton, InUI).Text(Text).TextSize(10.f).MinWidth(WidthPx - 10.f).IsEnabled(bEnabled)
					.OnClicked(FSimpleDelegate::CreateLambda(MoveTemp(OnClick)))
			];
	};
	// Options: M9 (docs/parity.md)
	TSharedRef<SWidget> Options = Button(LOCTEXT("Options", "Options"), [Weak]() { if (Weak.IsValid()) Weak->ShowOptions(TEXT("Graphics")); });
	TSharedRef<SWidget> LogOff = Button(LOCTEXT("LogOff", "Log Off"), [Weak]() { if (Weak.IsValid()) Weak->LogOffToCharacters(); });
	LogOff->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak]()
	{
		return Weak.IsValid() && Weak->CanLogOff() ? EVisibility::Visible : EVisibility::Collapsed;
	}));
	// only for the characters the server gave the admin or DM module (UMRNetSubsystem::IsStaff)
	TSharedRef<SWidget> AdminConsole = Button(LOCTEXT("Admin", "Admin Console"), [Weak]() { if (Weak.IsValid()) Weak->SetWindowOpen(EMRWindow::Admin, true); });
	AdminConsole->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak]()
	{
		const UMRNetSubsystem* Net = Weak.IsValid() ? Weak->GetNet() : nullptr;
		return Net && Net->IsStaff() ? EVisibility::Visible : EVisibility::Collapsed;
	}));

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SMRPanel, InUI).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SBox).WidthOverride(WidthPx * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 4.f * Px)
				[
					MRUI::Label(S, LOCTEXT("Title", "Meridian"), 13.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Resume", "Resume"), [Weak]() { if (Weak.IsValid()) Weak->SetGameMenuOpen(false); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Who", "Who Is On (O)"), [Weak]() { if (Weak.IsValid()) Weak->SetWindowOpen(EMRWindow::Who, true); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Mail", "Mail (L)"), [Weak]() { if (Weak.IsValid()) Weak->SetWindowOpen(EMRWindow::Mail, true); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Guild", "Guild (Y)"), [Weak]() { if (Weak.IsValid()) Weak->RunChatLine(TEXT("/guild")); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Options
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					AdminConsole
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					LogOff
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Quit", "Quit Game"), [Weak]() { if (Weak.IsValid()) Weak->QuitGame(); })
				]
			]
		]
	];
}

void SMRGameMenu::OnOpened()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SMRGameMenu::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if ((Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::F10) && UI.IsValid())
	{
		UI->SetGameMenuOpen(false);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMRGameMenu::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	return FReply::Handled();  // nothing through to the game behind it
}

int32 SMRGameMenu::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	// dim the world behind the menu, as the inventory dialog does
	if (UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr)
	{
		MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()),
			S->Color(TEXT("dim"), FLinearColor(0.f, 0.f, 0.f, 0.35f)));
	}
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 1, WStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE
