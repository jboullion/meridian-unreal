#include "UI/SMRChatLog.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateTextRun.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"
#include "Styling/CoreStyle.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRChat"

namespace
{
	constexpr int32 LinesKept = 120;     // lines in the log (scroll back while typing)
	constexpr double ShowSeconds = 15.0;  // then the log fades out until the next line
	constexpr float WidthPx = 280.f;
	constexpr float HeightPx = 96.f;
	constexpr int32 HistoryMax = 30;

	UMRNetSubsystem* NetOf(const UMRUISubsystem* UI)
	{
		const ULocalPlayer* LP = UI ? UI->GetLocalPlayer() : nullptr;
		const UGameInstance* GI = LP ? LP->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	}

	FString Escape(const FString& In)
	{
		return In.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")).Replace(TEXT("\""), TEXT("&quot;"));
	}

	/**
	 * The chat's runs: <r c="RRGGBBAA" s="bits">text</> with the colour and the style bits of
	 * MRServerText (bold takes the bold font, underline the core underline brush; italic has no face
	 * in the UI's font and stays upright).
	 */
	class FMRRunDecorator : public ITextDecorator
	{
	public:
		FMRRunDecorator(const FTextBlockStyle& InBase, const FTextBlockStyle& InBold) : Base(InBase), Bold(InBold) {}

		virtual bool Supports(const FTextRunParseResults& Run, const FString& Text) const override
		{
			return Run.Name == TEXT("r");
		}

		virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& Layout, const FTextRunParseResults& Run, const FString& OriginalText,
			const TSharedRef<FString>& InOutModelText, const ISlateStyle* Style) override
		{
			FRunInfo Info(Run.Name);
			for (const TPair<FString, FTextRange>& P : Run.MetaData)
			{
				Info.MetaData.Add(P.Key, OriginalText.Mid(P.Value.BeginIndex, P.Value.EndIndex - P.Value.BeginIndex));
			}
			const int32 Bits = Info.MetaData.Contains(TEXT("s")) ? FCString::Atoi(*Info.MetaData[TEXT("s")]) : 0;
			FTextBlockStyle TS = (Bits & MRServerText::STYLE_BOLD) ? Bold : Base;
			if (const FString* C = Info.MetaData.Find(TEXT("c")))
			{
				TS.SetColorAndOpacity(FLinearColor(FColor::FromHex(*C)));
			}
			if (Bits & MRServerText::STYLE_UNDERLINE)
			{
				if (const FSlateBrush* U = FCoreStyle::Get().GetOptionalBrush(TEXT("DefaultTextUnderline"), nullptr, nullptr))
				{
					TS.SetUnderlineBrush(*U);
				}
			}
			FTextRange Range;
			Range.BeginIndex = InOutModelText->Len();
			*InOutModelText += OriginalText.Mid(Run.ContentRange.BeginIndex, Run.ContentRange.EndIndex - Run.ContentRange.BeginIndex);
			Range.EndIndex = InOutModelText->Len();
			return FSlateTextRun::Create(Info, InOutModelText, TS, Range);
		}

	private:
		FTextBlockStyle Base;
		FTextBlockStyle Bold;
	};
}

SMRChatLog::~SMRChatLog()
{
	if (UMRNetSubsystem* Net = NetOf(UI.Get()))
	{
		Net->OnChat.Remove(ChatHandle);
	}
}

void SMRChatLog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	BaseStyle = FTextBlockStyle()
		.SetFont(S->Font(9.f))
		.SetColorAndOpacity(FSlateColor(S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f))))
		.SetShadowOffset(FVector2D(1.0, 1.0) * Px * 0.5)
		.SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	BoldStyle = BaseStyle;
	BoldStyle.SetFont(S->Font(9.f, true));
	SetVisibility(EVisibility::SelfHitTestInvisible);

	// the tabs (clickable while typing)
	TSharedRef<SHorizontalBox> TabRow = SNew(SHorizontalBox);
	const TPair<FText, int32> TabDefs[] = {{LOCTEXT("All", "All"), -1}, {LOCTEXT("Chat", "Chat"), static_cast<int32>(EMRChatChannel::Chat)},
		{LOCTEXT("Combat", "Combat"), static_cast<int32>(EMRChatChannel::Combat)}, {LOCTEXT("Game", "Game"), static_cast<int32>(EMRChatChannel::Game)}};
	for (const TPair<FText, int32>& T : TabDefs)
	{
		const int32 Id = T.Value;
		TabRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, InUI).Text(T.Key).TextSize(8.f).MinWidth(34.f)
				.bActive_Lambda([this, Id]() { return Tab == Id; })
				.OnClicked(FSimpleDelegate::CreateSP(this, &SMRChatLog::SetTab, Id))
		];
	}
	Tabs = TabRow;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f * Px)[TabRow]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(3.f)
				.BackgroundTint(FLinearColor(1.f, 1.f, 1.f, 0.8f))
			[
				// a few lines high, scrolled to the newest (the wheel scrolls back while typing)
				SNew(SBox).WidthOverride(WidthPx * Px).HeightOverride(HeightPx * Px)
				[
					SAssignNew(Scroll, SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()[SAssignNew(Lines, SVerticalBox)]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
		[
			SAssignNew(Input, SMRTextField, InUI).Width(WidthPx).MaxLength(250)
			.HintText(LOCTEXT("Hint", "Say something; /who, /tell name..., /mail, /help (Enter); Esc to cancel"))
			.OnSubmit(FSimpleDelegate::CreateSP(this, &SMRChatLog::Submit))
			.OnCancel(FSimpleDelegate::CreateSP(this, &SMRChatLog::CloseInput))
		]
	];
	Input->SetVisibility(EVisibility::Collapsed);
	Tabs->SetVisibility(EVisibility::Hidden);
	if (UMRNetSubsystem* Net = NetOf(InUI))
	{
		ChatHandle = Net->OnChat.AddSP(this, &SMRChatLog::OnChat);
		if (Net->GetChat().Num() > 0)
		{
			LastLineTime = Net->GetChat().Last().Time;
		}
	}
	Rebuild();
}

FLinearColor SMRChatLog::ColorFor(uint8 Kind) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const FLinearColor Text = S ? S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)) : FLinearColor::White;
	switch (Kind)
	{
	case MRMsg::SAY_NORMAL: return Text;
	case MRMsg::SAY_RESOURCE: return FLinearColor(0.95f, 0.88f, 0.75f);
	case MRMsg::SAY_YELL: return FLinearColor(1.f, 0.55f, 0.25f);
	case MRMsg::SAY_EVERYONE: return S ? S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)) : Text;
	case MRMsg::SAY_EMOTE: return FLinearColor(0.8f, 0.78f, 0.7f);
	case MRMsg::SAY_GROUP: return FLinearColor(0.6f, 0.9f, 1.f);   // tells
	case MRMsg::SAY_GUILD: return FLinearColor(0.6f, 1.f, 0.6f);
	case MRMsg::SAY_DM: return FLinearColor(1.f, 0.6f, 1.f);
	default: return FLinearColor(0.78f, 0.86f, 1.f);  // game messages
	}
}

bool SMRChatLog::Shows(const FMRChatLine& Line) const
{
	return Tab < 0 || static_cast<int32>(Line.Channel) == Tab;
}

FString SMRChatLog::Markup(const FMRChatLine& Line) const
{
	const UMRNetSubsystem* Net = NetOf(UI.Get());
	FString Out;
	if (Net && Net->GetSocial().bTimestamps)
	{
		// srvrstr.c AddTimeStamp: "[HH:MM] " in the system colour
		Out += FString::Printf(TEXT("<r c=\"%s\">%s</>"), *FLinearColor(0.7f, 0.55f, 0.85f).ToFColor(true).ToHex(),
			*Escape(Line.When.ToString(TEXT("[%H:%M] "))));
	}
	const FString Own = ColorFor(Line.Kind).ToFColor(true).ToHex();
	for (const FMRTextRun& R : MRServerText::Runs(Line.Styled.IsEmpty() ? Line.Text : Line.Styled))
	{
		// the original's dark colours (black, teal, dark blue) on our dark panel: lifted a little
		FLinearColor C = R.Color.Get(ColorFor(Line.Kind));
		if (R.Color.IsSet() && C.GetLuminance() < 0.08f)
		{
			C = FMath::Lerp(C, FLinearColor::White, 0.35f);
		}
		Out += FString::Printf(TEXT("<r c=\"%s\" s=\"%d\">%s</>"), R.Color.IsSet() ? *C.ToFColor(true).ToHex() : *Own, R.Style, *Escape(R.Text));
	}
	return Out;
}

void SMRChatLog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* Net = NetOf(Ui);
	if (!Ui || !Lines.IsValid())
	{
		return;
	}
	Lines->ClearChildren();
	if (!Net)
	{
		return;
	}
	const TArray<FMRChatLine>& Chat = Net->GetChat();
	TArray<int32> Shown;
	for (int32 i = Chat.Num() - 1; i >= 0 && Shown.Num() < LinesKept; --i)
	{
		if (Shows(Chat[i]))
		{
			Shown.Insert(i, 0);
		}
	}
	const TArray<TSharedRef<ITextDecorator>> Decorators = {MakeShared<FMRRunDecorator>(BaseStyle, BoldStyle)};
	for (const int32 i : Shown)
	{
		Lines->AddSlot().AutoHeight()
		[
			SNew(SRichTextBlock).Text(FText::FromString(Markup(Chat[i]))).TextStyle(&BaseStyle).AutoWrapText(true).Decorators(Decorators)
		];
	}
	ScrollFrames = 3;  // to the newest once the new lines have been laid out (Tick)
}

void SMRChatLog::SetTab(int32 InTab)
{
	Tab = InTab;
	Rebuild();
}

void SMRChatLog::OnChat(const FMRChatLine& Line)
{
	if (Shows(Line))
	{
		LastLineTime = Line.Time;
	}
	Rebuild();
}

void SMRChatLog::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geo, Time, Dt);
	if (ScrollFrames > 0 && Scroll.IsValid())
	{
		--ScrollFrames;
		Scroll->SetScrollOffset(Scroll->GetScrollOffsetOfEnd());
	}
	const bool bHasLines = Lines.IsValid() && Lines->NumSlots() > 0;
	const bool bShow = bTyping || (bHasLines && FPlatformTime::Seconds() - LastLineTime < ShowSeconds);
	Opacity = FMath::FInterpTo(Opacity, bShow ? 1.f : 0.f, Dt, bShow ? 12.f : 2.f);
	SetRenderOpacity(Opacity);
}

FReply SMRChatLog::OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	// Up and Down: the lines typed before
	if (bTyping && Input.IsValid() && History.Num() > 0 && (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down))
	{
		HistoryAt = FMath::Clamp(HistoryAt + (Event.GetKey() == EKeys::Up ? -1 : 1), 0, History.Num());
		Input->SetText(History.IsValidIndex(HistoryAt) ? History[HistoryAt] : FString());
		Input->MoveToEnd();
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(Geo, Event);
}

void SMRChatLog::OpenInput()
{
	if (!Input.IsValid())
	{
		return;
	}
	bTyping = true;
	HistoryAt = History.Num();
	SetVisibility(EVisibility::Visible);  // the wheel scrolls back, the tabs click
	Tabs->SetVisibility(EVisibility::Visible);
	Input->SetText(FString());
	Input->SetVisibility(EVisibility::Visible);
	Input->Focus();
}

void SMRChatLog::SetInputText(const FString& Text)
{
	if (Input.IsValid())
	{
		Input->SetText(Text);
		Input->Focus();
		Input->MoveToEnd();
	}
}

void SMRChatLog::CloseInput()
{
	if (!bTyping)
	{
		return;
	}
	bTyping = false;
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Tabs->SetVisibility(EVisibility::Hidden);
	ScrollFrames = 3;
	if (Input.IsValid())
	{
		Input->SetVisibility(EVisibility::Collapsed);
	}
	if (UMRUISubsystem* Ui = UI.Get())
	{
		Ui->OnChatClosed();
	}
}

void SMRChatLog::Submit()
{
	const FString Line = Input.IsValid() ? Input->GetText() : FString();
	CloseInput();
	if (!Line.TrimStartAndEnd().IsEmpty())
	{
		History.Remove(Line);
		History.Add(Line);
		if (History.Num() > HistoryMax)
		{
			History.RemoveAt(0);
		}
	}
	if (UMRUISubsystem* Ui = UI.Get())
	{
		Ui->RunChatLine(Line);  // (after closing: a command may open a window)
	}
}

#undef LOCTEXT_NAMESPACE
