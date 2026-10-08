#include "UI/SMRLookDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRControls.h"
#include "UI/SMRHUD.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRLook"

namespace
{
	constexpr float WidthPx = 230.f;     // the window's inside, original pixels
	constexpr float PicturePx = 64.f;
	constexpr float TextHeightPx = 90.f;

	FLinearColor NameColorOf(const FMRNetObject& O)
	{
		if (O.DrawEffect == MRMsg::DRAWFX_BLACK || O.NameColor == 0)
		{
			return FLinearColor(1.f, 0.93f, 0.7f);
		}
		return FLinearColor(FColor((O.NameColor >> 16) & 0xFF, (O.NameColor >> 8) & 0xFF, O.NameColor & 0xFF, 255));
	}
}

void SMRLookDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
}

void SMRLookDialog::Rebuild(TSharedRef<SWidget> Body, const FText& Title, const FLinearColor& TitleColor, TSharedPtr<SWidget> Extra)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return;
	}
	const float Px = S->Px();
	TWeakObjectPtr<UMRUISubsystem> Weak(Ui);
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SMRPanel, Ui).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SBox).WidthOverride(WidthPx * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 4.f * Px)
				[
					MRUI::Label(S, Title, 12.f, true, TitleColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Body
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 4.f * Px, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f * Px, 0.f)
					[
						Extra.IsValid() ? Extra.ToSharedRef() : SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMRTextButton, Ui).Text(LOCTEXT("Close", "Close")).TextSize(10.f).MinWidth(50.f)
							.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) Weak->CloseLook(); }))
					]
				]
			]
		]
	];
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMRLookDialog::ShowDescription(const FMRNetDescription& D, const FSlateBrush* Picture)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return;
	}
	const float Px = S->Px();
	PickIds.Reset();
	EditBox.Reset();
	EditId = 0;
	const FLinearColor Body(0.86f, 0.84f, 0.78f);
	const FLinearColor Detail = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	auto Para = [S, Px](const FString& Text, const FLinearColor& Color, float Size = 8.5f)
	{
		return SNew(STextBlock).Text(FText::FromString(MRServerText::StripStyle(Text))).Font(S->Font(Size)).ColorAndOpacity(Color)
			.AutoWrapText(true).ShadowOffset(FVector2D(1.0, 1.0) * Px * 0.5).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	};

	TSharedRef<SVerticalBox> Text = SNew(SVerticalBox);
	TSharedPtr<SWidget> SaveButton;
	if (D.bPlayer && D.bEditable)
	{
		// one's own description: the server keeps what is written here (BP_CHANGE_DESCRIPTION)
		EditId = D.Object.Id;
		Text->AddSlot().AutoHeight()
		[
			SAssignNew(EditBox, SMRTextBox, Ui).InitialText(FText::FromString(MRServerText::StripStyle(D.Text)))
				.Width(WidthPx - PicturePx - 12.f).Height(TextHeightPx - 6.f).MaxLength(1000)
		];
		TWeakObjectPtr<UMRUISubsystem> Weak(Ui);
		TWeakPtr<SMRLookDialog> WeakSelf = SharedThis(this);
		SaveButton = SNew(SMRTextButton, Ui).Text(LOCTEXT("Save", "Save")).TextSize(10.f).MinWidth(50.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak, WeakSelf]()
				{
					const TSharedPtr<SMRLookDialog> Self = WeakSelf.Pin();
					if (Weak.IsValid() && Self.IsValid() && Self->EditId)
					{
						Weak->SaveDescription(Self->EditId, Self->GetEditedText());
					}
				}));
	}
	else if (!D.Text.IsEmpty())
	{
		Text->AddSlot().AutoHeight()[Para(D.Text, Body)];
	}
	if (!D.Inscription.IsEmpty())
	{
		Text->AddSlot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Para(D.Inscription, FLinearColor(0.95f, 0.88f, 0.62f), 9.f)];
	}
	if (!D.ExtraInfo.IsEmpty())
	{
		Text->AddSlot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Para(D.ExtraInfo, Detail, 8.f)];
	}
	if (!D.Url.IsEmpty())
	{
		Text->AddSlot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)[Para(D.Url, FLinearColor(0.55f, 0.75f, 1.f), 8.f)];
	}
	if (D.Object.Amount > 0)
	{
		Text->AddSlot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)[Para(FString::Printf(TEXT("%u"), D.Object.Amount), Detail, 8.f)];
	}

	if (!D.bPlayer && !SaveButton.IsValid())
	{
		// a thing in the room: what can be done with it, as the original's look dialog offered (dialog.c)
		TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
		TWeakObjectPtr<UMRUISubsystem> Weak(Ui);
		const uint32 Id = D.Object.Id;
		auto AddButton = [&](const FText& Label, EMRObjectAction Action)
		{
			Buttons->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f * Px, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(Label).TextSize(10.f).MinWidth(50.f)
					.OnClicked(FSimpleDelegate::CreateLambda([Weak, Id, Action]() { if (Weak.IsValid()) Weak->DoObjectAction(Action, Id); }))
			];
		};
		if (D.Object.Flags & MRMsg::OF_GETTABLE)
		{
			AddButton(LOCTEXT("Get", "Get"), EMRObjectAction::Get);
		}
		if (D.Object.Flags & MRMsg::OF_CONTAINER)
		{
			AddButton(LOCTEXT("Inside", "Inside"), EMRObjectAction::Inside);
		}
		if (D.Object.Flags & MRMsg::OF_ACTIVATABLE)
		{
			AddButton(LOCTEXT("Use", "Use"), EMRObjectAction::Activate);
		}
		if (Buttons->NumSlots() > 0)
		{
			SaveButton = Buttons;
		}
	}

	const TSharedRef<SWidget> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 6.f * Px, 0.f)
		[
			SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
			[
				SNew(SBox).WidthOverride(PicturePx * Px).HeightOverride(PicturePx * Px)
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage).Image(Picture)
					]
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SBox).MaxDesiredHeight(TextHeightPx * Px)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[Text]
			]
		];
	Rebuild(Row, FText::FromString(D.Object.Name), NameColorOf(D.Object), SaveButton);
}

void SMRLookDialog::ShowPicker(const TArray<uint32>& Ids, const TArray<FText>& Names, EMRPickAction Action, const FText& Title)
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui)
	{
		return;
	}
	PickIds = Ids;
	PickAction = Action;
	EditBox.Reset();
	TWeakObjectPtr<UMRUISubsystem> Weak(Ui);
	TWeakPtr<SMRLookDialog> WeakSelf = SharedThis(this);
	TSharedRef<SMRSelectList> List = SNew(SMRSelectList, Ui).Width(WidthPx).Height(FMath::Min(110.f, 14.f * Names.Num() + 6.f)).TextSize(10.f)
		.OnActivated(SMRSelectList::FOnRow::CreateLambda([Weak, WeakSelf](int32 Row)
		{
			const TSharedPtr<SMRLookDialog> Self = WeakSelf.Pin();
			if (Weak.IsValid() && Self.IsValid() && Self->PickIds.IsValidIndex(Row))
			{
				Weak->PickChosen(Self->PickAction, Self->PickIds[Row]);
			}
		}));
	List->SetItems(Names);
	Rebuild(List, Title, FLinearColor(1.f, 0.93f, 0.7f), nullptr);
}

FString SMRLookDialog::GetEditedText() const
{
	return EditBox.IsValid() ? EditBox->GetText() : FString();
}

void SMRLookDialog::SetEditedText(const FString& Text)
{
	if (EditBox.IsValid())
	{
		EditBox->SetText(Text);
	}
}

FReply SMRLookDialog::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape && UI.IsValid())
	{
		UI->CloseLook();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMRLookDialog::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	return FReply::Handled();  // nothing through to the game behind it
}

#undef LOCTEXT_NAMESPACE
