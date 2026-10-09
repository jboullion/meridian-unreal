#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SMRTextField;
class SScrollBox;
class SVerticalBox;
class UMRUISubsystem;
struct FMRChatLine;
enum class EMRChatChannel : uint8;

/**
 * The chat log (bottom left, online only): speech and the game's messages from the server
 * (UMRNetSubsystem::OnChat) in the server's colours and styles ("~" codes), with tabs for All,
 * Chat, Combat and Game, and timestamps when the player wants them. It fades a while after the last
 * line. Enter opens a line to type (UMRUISubsystem::OpenChat): Enter runs it (UMRUISubsystem::RunChatLine:
 * speech or a command), Up and Down recall earlier lines, Escape cancels; while typing, the wheel
 * scrolls back and the tabs can be clicked.
 */
class UNREALMERIDIAN_API SMRChatLog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRChatLog) {}
	SLATE_END_ARGS()

	~SMRChatLog();
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual FReply OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;

	void OpenInput();
	void CloseInput();
	bool IsTyping() const { return bTyping; }
	/** What's typed so far (a tell's name from the who list), the cursor at its end. */
	void SetInputText(const FString& Text);
	/** The tab shown: -1 All, else an EMRChatChannel. */
	void SetTab(int32 Tab);
	int32 GetTab() const { return Tab; }

private:
	void Rebuild();
	void OnChat(const FMRChatLine& Line);
	void Submit();
	FLinearColor ColorFor(uint8 Kind) const;
	bool Shows(const FMRChatLine& Line) const;
	/** The line as rich text: each run with its colour and style. */
	FString Markup(const FMRChatLine& Line) const;

	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SVerticalBox> Lines;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SMRTextField> Input;
	TSharedPtr<SWidget> Tabs;
	FDelegateHandle ChatHandle;
	FTextBlockStyle BaseStyle;
	FTextBlockStyle BoldStyle;
	int32 Tab = -1;
	bool bTyping = false;
	double LastLineTime = -100.0;
	float Opacity = 0.f;
	int32 ScrollFrames = 0;
	/** Lines typed before (Up and Down), newest last. */
	TArray<FString> History;
	int32 HistoryAt = 0;
};
