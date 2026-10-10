#include "UI/SMRChatLog.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateTextRun.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "MRChat"

namespace
{
	constexpr int32 LinesKept = 120;     // lines in the log (scroll back while typing)
	constexpr int32 HistoryMax = 30;
	constexpr int32 MaxLineLength = 250;

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

	/** A tab over the chat (Shards' .chat-tabs button): gold-edged and rounded on top, the shown one tinted gold. */
	class SMRChatTab : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMRChatTab) {}
			SLATE_ARGUMENT(FText, Label)
			SLATE_ATTRIBUTE(bool, bActive)
			SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
		{
			UI = InUI;
			Label = InArgs._Label.ToString();
			bActive = InArgs._bActive;
			OnClicked = InArgs._OnClicked;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (!S)
			{
				return FVector2D(40.f, 20.f);
			}
			const FVector2f M = MRPaint::MeasureText(Label, S->HudFont(S->HudNumber(TEXT("chat_tab_size"), 13.f)));
			return FVector2D(M.X + 20.f * S->HudPx(), M.Y + 5.f * S->HudPx());
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (!S)
			{
				return Layer;
			}
			const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
			const FVector2f Size(Geo.GetLocalSize());
			const float Px = S->HudPx();
			const bool bOn = bActive.Get(false);
			const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
			// rounded on top (border-radius 3px 3px 0 0); the gold line under the tabs closes them
			FSlateBrush B;
			B.DrawAs = ESlateBrushDrawType::RoundedBox;
			B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(3.f * Px, 3.f * Px, 0.f, 0.f),
				FSlateColor(S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f)) * Tint), Px);
			FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), &B, ESlateDrawEffect::None,
				S->HudColor(bOn ? TEXT("chat_tab_active") : TEXT("chat_tab"), FLinearColor(0.f, 0.f, 0.f, 0.5f)) * Tint);
			const FSlateFontInfo Font = S->HudFont(S->HudNumber(TEXT("chat_tab_size"), 13.f));
			const FVector2f M = MRPaint::MeasureText(Label, Font);
			const FLinearColor Text = bOn || IsHovered() ? Gold : S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f));
			MRPaint::Text(Out, Layer + 1, Geo, Label, Font, FVector2f((Size.X - M.X) * 0.5f, 2.f * Px), Text * Tint, Px);
			return Layer + 3;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			OnClicked.ExecuteIfBound();
			return FReply::Handled();
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
		FString Label;
		TAttribute<bool> bActive;
		FSimpleDelegate OnClicked;
	};

	/** The chat's right edge: dragged, it sets the width (Shards' .chat-resize). */
	class SMRChatEdge : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMRChatEdge) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, SMRChatLog* InChat, float InWidth)
		{
			Chat = InChat;
			Width = InWidth;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 10.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			return Layer;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (!HasMouseCapture() || !Chat)
			{
				return FReply::Unhandled();
			}
			// the width is from the chat's left edge to the mouse
			Chat->DragWidth(Chat->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition()).X);
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}

	private:
		SMRChatLog* Chat = nullptr;
		float Width = 10.f;
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
	MaxWidth = InArgs._MaxWidth;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	const float TextSize = S->HudNumber(TEXT("chat_text_size"), 14.f);
	BaseStyle = FTextBlockStyle()
		.SetFont(S->HudFont(TextSize))
		.SetColorAndOpacity(FSlateColor(FLinearColor::White))
		.SetShadowOffset(FVector2D(1.0, 1.0) * Px)
		.SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	BoldStyle = BaseStyle;
	BoldStyle.SetFont(S->HudFont(TextSize, true));
	InputBrush = FSlateColorBrush(S->HudColor(TEXT("chat_input"), FLinearColor(0.f, 0.f, 0.f, 0.6f)));
	EdgeBrush = FSlateColorBrush(S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f)));
	// hit-testable while typing or with the cursor free (hovering shows the panel; the tabs and the edge click)
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		const APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr;
		return bTyping || (PC && PC->bShowMouseCursor) ? EVisibility::Visible : EVisibility::SelfHitTestInvisible;
	}));

	// the tabs
	TSharedRef<SHorizontalBox> TabRow = SNew(SHorizontalBox);
	const TPair<FText, int32> TabDefs[] = {{LOCTEXT("All", "All"), -1}, {LOCTEXT("Chat", "Chat"), static_cast<int32>(EMRChatChannel::Chat)},
		{LOCTEXT("Combat", "Combat"), static_cast<int32>(EMRChatChannel::Combat)}, {LOCTEXT("Game", "Game"), static_cast<int32>(EMRChatChannel::Game)}};
	for (const TPair<FText, int32>& T : TabDefs)
	{
		const int32 Id = T.Value;
		TabRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRChatTab, InUI).Label(T.Key)
				.bActive_Lambda([this, Id]() { return Tab == Id; })
				.OnClicked(FSimpleDelegate::CreateSP(this, &SMRChatLog::SetTab, Id))
		];
	}
	// the tabs, the lines over the line to type and the input show with the panel (Shards: opacity 0 idle)
	auto WithPanel = [this]() { return FLinearColor(1.f, 1.f, 1.f, PanelAlpha); };
	const FSlateBrush* None = FCoreStyle::Get().GetBrush("NoBorder");

	ChildSlot
	[
		SNew(SBox).WidthOverride_Lambda([this]() { return FOptionalSize(WidthNow()); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(None).Padding(FMargin(4.f * Px, 4.f * Px, 4.f * Px, 0.f)).ColorAndOpacity_Lambda(WithPanel)
					[
						TabRow
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(&EdgeBrush).Padding(0.f).ColorAndOpacity_Lambda(WithPanel).BorderBackgroundColor_Lambda(WithPanel)
					[
						SNew(SBox).HeightOverride(Px)
					]
				]
				// the lines, ending at the bottom (the newest over the view's lower left)
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
				[
					SAssignNew(Scroll, SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot().Padding(8.f * Px, 4.f * Px)[SAssignNew(Lines, SVerticalBox)]
				]
				// the line to type in, under a gold line
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(&EdgeBrush).Padding(0.f).ColorAndOpacity_Lambda(WithPanel).BorderBackgroundColor_Lambda(WithPanel)
					[
						SNew(SBox).HeightOverride(Px)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(&InputBrush).Padding(FMargin(8.f * Px, 6.f * Px)).ColorAndOpacity_Lambda(WithPanel)
						.BorderBackgroundColor_Lambda(WithPanel)
					[
						SAssignNew(Input, SEditableText)
						.Font(S->HudFont(TextSize))
						.ColorAndOpacity(FLinearColor::White)
						.HintText(LOCTEXT("Hint", "Say something; /who, /tell name..., /mail, /help (Enter); Esc to cancel"))
						.ClearKeyboardFocusOnCommit(false)
						.OnTextChanged_Lambda([this](const FText& T)
						{
							if (T.ToString().Len() > MaxLineLength)
							{
								Input->SetText(FText::FromString(T.ToString().Left(MaxLineLength)));
							}
						})
						.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type How)
						{
							if (How == ETextCommit::OnEnter)
							{
								Submit();
							}
							else if (How == ETextCommit::OnCleared)
							{
								CloseInput();  // Escape
							}
						})
					]
				]
			]
			// the right edge drags the width (with the cursor free)
			+ SOverlay::Slot().HAlign(HAlign_Right).Padding(0.f, 0.f, -5.f * Px, 0.f)
			[
				SNew(SMRChatEdge, this, 10.f * Px).Visibility_Lambda([this]()
				{
					const APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr;
					return PC && PC->bShowMouseCursor ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		]
	];
	Input->SetEnabled(false);
	if (UMRNetSubsystem* Net = NetOf(InUI))
	{
		ChatHandle = Net->OnChat.AddSP(this, &SMRChatLog::OnChat);
	}
	Rebuild();
}

float SMRChatLog::WidthNow() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return 340.f;
	}
	const float Px = S->HudPx();
	const float Min = S->HudNumber(TEXT("chat_min_width"), 180.f) * Px;
	const float Max = MaxWidth.IsSet() ? MaxWidth.Get() : 1e6f;
	return FMath::Max(Min, FMath::Min(Ui->GetChatWidth() * Px, Max));
}

void SMRChatLog::DragWidth(float Width)
{
	UMRUISubsystem* Ui = UI.Get();
	const UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (S)
	{
		const float Px = S->HudPx();
		const float Min = S->HudNumber(TEXT("chat_min_width"), 180.f) * Px;
		const float Max = MaxWidth.IsSet() ? MaxWidth.Get() : 1e6f;
		Ui->SetChatWidth(FMath::Clamp(Width, Min, FMath::Max(Max, Min)) / Px);
	}
}

bool SMRChatLog::IsActive() const
{
	const APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr;
	return bTyping || (PC && PC->bShowMouseCursor && IsHovered());
}

float SMRChatLog::LineAlpha(double Time) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const double Fresh = S ? S->HudNumber(TEXT("chat_fresh_seconds"), 10.f) : 10.0;
	const double Fade = S ? FMath::Max(0.05f, S->HudNumber(TEXT("chat_fade_seconds"), 0.6f)) : 0.6;
	const double Age = FPlatformTime::Seconds() - Time;
	return static_cast<float>(FMath::Clamp(1.0 - (Age - Fresh) / Fade, 0.0, 1.0));
}

FLinearColor SMRChatLog::ColorFor(uint8 Kind) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const FLinearColor Text = FLinearColor::White;  // Shards' chat: white over the view
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
		// the original's dark colours (black, teal, dark blue) over the view: lifted a little
		FLinearColor C = R.Color.Get(ColorFor(Line.Kind));
		if (R.Color.IsSet() && C.GetLuminance() < 0.08f)
		{
			C = FMath::Lerp(C, FLinearColor::White, 0.35f);
		}
		// a run is closed and opened again at each line break: the markup parser reads one line at a
		// time, and a run across lines showed its tags (the server's long welcome message)
		const FString Open = FString::Printf(TEXT("<r c=\"%s\" s=\"%d\">"), R.Color.IsSet() ? *C.ToFColor(true).ToHex() : *Own, R.Style);
		Out += Open + Escape(R.Text).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), *(TEXT("</>\n") + Open)) + TEXT("</>");
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
	const FSlateBrush* None = FCoreStyle::Get().GetBrush("NoBorder");
	for (const int32 i : Shown)
	{
		// idle, a line shows while it's fresh, then fades (Shards' .chat-line.fresh); the panel shows them all
		const double Time = Chat[i].Time;
		Lines->AddSlot().AutoHeight()
		[
			SNew(SBorder).BorderImage(None).Padding(0.f)
				.ColorAndOpacity_Lambda([this, Time]() { return FLinearColor(1.f, 1.f, 1.f, FMath::Max(PanelAlpha, LineAlpha(Time))); })
			[
				SNew(SRichTextBlock).Text(FText::FromString(Markup(Chat[i]))).TextStyle(&BaseStyle).AutoWrapText(true).Decorators(Decorators)
			]
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
	const bool bActive = IsActive();
	const bool bWasShown = PanelAlpha > 0.f;
	PanelAlpha = FMath::FInterpConstantTo(PanelAlpha, bActive ? 1.f : 0.f, Dt, 5.f);  // Shards: 0.2 s
	if (bWasShown && PanelAlpha <= 0.f)
	{
		ScrollFrames = 2;  // idle again: back to the newest
	}
	// Hide Interface: away, unless typing (a chat key brings the line out)
	const bool bHidden = UI.IsValid() && UI->IsHudHidden() && !bTyping;
	Opacity = FMath::FInterpConstantTo(Opacity, bHidden ? 0.f : 1.f, Dt, 6.f);
	SetRenderOpacity(Opacity);
}

int32 SMRChatLog::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (S && PanelAlpha > 0.f)
	{
		// the panel while it's wanted (Shards' .chat:hover, .chat:focus-within)
		const FLinearColor A(1.f, 1.f, 1.f, PanelAlpha * WStyle.GetColorAndOpacityTint().A);
		MRPaint::Rounded(Out, Layer, Geo, FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()),
			S->HudColor(TEXT("chat_panel"), FLinearColor(0.004f, 0.003f, 0.002f, 0.72f)) * A, 4.f * S->HudPx(),
			S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f)) * A, S->HudPx());
	}
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 1, WStyle, bParentEnabled);
}

FReply SMRChatLog::OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	// Up and Down: the lines typed before
	if (bTyping && Input.IsValid() && History.Num() > 0 && (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down))
	{
		HistoryAt = FMath::Clamp(HistoryAt + (Event.GetKey() == EKeys::Up ? -1 : 1), 0, History.Num());
		Input->SetText(FText::FromString(History.IsValidIndex(HistoryAt) ? History[HistoryAt] : FString()));
		Input->GoTo(ETextLocation::EndOfDocument);
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
	Input->SetEnabled(true);
	Input->SetText(FText::GetEmpty());
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Input, EFocusCause::SetDirectly);
	}
}

void SMRChatLog::SetInputText(const FString& Text)
{
	if (Input.IsValid())
	{
		Input->SetText(FText::FromString(Text));
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocus(Input, EFocusCause::SetDirectly);
		}
		Input->GoTo(ETextLocation::EndOfDocument);
	}
}

void SMRChatLog::CloseInput()
{
	if (!bTyping)
	{
		return;
	}
	bTyping = false;
	ScrollFrames = 3;
	if (Input.IsValid())
	{
		Input->SetText(FText::GetEmpty());
		Input->SetEnabled(false);
	}
	if (UMRUISubsystem* Ui = UI.Get())
	{
		Ui->OnChatClosed();
	}
}

void SMRChatLog::Submit()
{
	const FString Line = Input.IsValid() ? Input->GetText().ToString() : FString();
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
