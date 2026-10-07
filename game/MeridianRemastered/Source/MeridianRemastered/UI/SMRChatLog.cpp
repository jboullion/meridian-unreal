#include "UI/SMRChatLog.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRChat"

namespace
{
	constexpr int32 LinesShown = 8;
	constexpr double ShowSeconds = 15.0;  // then the log fades out until the next line
	constexpr float WidthPx = 220.f;
	constexpr float HeightPx = 84.f;

	UMRNetSubsystem* NetOf(const UMRUISubsystem* UI)
	{
		const ULocalPlayer* LP = UI ? UI->GetLocalPlayer() : nullptr;
		const UGameInstance* GI = LP ? LP->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	}
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
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(3.f)
				.BackgroundTint(FLinearColor(1.f, 1.f, 1.f, 0.8f))
			[
				// a few lines high, scrolled to the newest (the tutorial's long messages push the rest up)
				SNew(SBox).WidthOverride(WidthPx * Px).MaxDesiredHeight(HeightPx * Px)
				[
					SAssignNew(Scroll, SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()[SAssignNew(Lines, SVerticalBox)]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
		[
			SAssignNew(Input, SMRTextField, InUI).Width(WidthPx).MaxLength(250)
			.HintText(LOCTEXT("Hint", "Say something (Enter), Esc to cancel"))
			.OnSubmit(FSimpleDelegate::CreateSP(this, &SMRChatLog::Submit))
			.OnCancel(FSimpleDelegate::CreateSP(this, &SMRChatLog::CloseInput))
		]
	];
	Input->SetVisibility(EVisibility::Collapsed);
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
	case MRMsg::SAY_YELL: return FLinearColor(1.f, 0.55f, 0.25f);
	case MRMsg::SAY_EVERYONE: return S ? S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)) : Text;
	case MRMsg::SAY_EMOTE: return FLinearColor(0.8f, 0.78f, 0.7f);
	default: return FLinearColor(0.78f, 0.86f, 1.f);  // game messages
	}
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
	for (int32 i = FMath::Max(0, Chat.Num() - LinesShown); i < Chat.Num(); ++i)
	{
		TSharedRef<STextBlock> T = MRUI::Label(Ui->GetStyle(), FText::FromString(Chat[i].Text), 9.f, false, ColorFor(Chat[i].Kind));
		T->SetAutoWrapText(true);
		Lines->AddSlot().AutoHeight()[T];
	}
	ScrollFrames = 3;  // to the newest once the new lines have been laid out (Tick)
}

void SMRChatLog::OnChat(const FMRChatLine& Line)
{
	LastLineTime = Line.Time;
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

void SMRChatLog::OpenInput()
{
	if (!Input.IsValid())
	{
		return;
	}
	bTyping = true;
	Input->SetText(FString());
	Input->SetVisibility(EVisibility::Visible);
	Input->Focus();
}

void SMRChatLog::CloseInput()
{
	if (!bTyping)
	{
		return;
	}
	bTyping = false;
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
	if (UMRNetSubsystem* Net = NetOf(UI.Get()); Net && Input.IsValid())
	{
		Net->Say(Input->GetText());
	}
	CloseInput();
}

#undef LOCTEXT_NAMESPACE
