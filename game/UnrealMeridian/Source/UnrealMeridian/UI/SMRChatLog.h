#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SEditableText;
class SScrollBox;
class SVerticalBox;
class UMRUISubsystem;
struct FMRChatLine;
enum class EMRChatChannel : uint8;

/**
 * The chat (online only), down the left side of the view as Meridian Shards' Modern interface has
 * it (its styles.css .game.modern-ui .chat): speech and the game's messages from the server
 * (UMRNetSubsystem::OnChat) in the server's colours and styles ("~" codes), with tabs for All, Chat,
 * Combat and Game, and timestamps when the player wants them.
 * - Idle there's no panel: only lines from the last few seconds (ui_style.json hud chat_fresh_seconds)
 *   show over the view, the newest at the bottom, and older ones fade.
 * - Typing (Enter: UMRUISubsystem::OpenChat), or with the cursor free and over it, it's a dark panel
 *   with the tabs, the whole scrollback (the wheel) and the line. Enter runs the line
 *   (UMRUISubsystem::RunChatLine: speech or a command), Up and Down recall earlier lines, Escape cancels.
 * - With the cursor free its right edge drags its width (mr.UI.ChatWidth).
 */
class UNREALMERIDIAN_API SMRChatLog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRChatLog) {}
		/** The widest it may be, in its own units (Shards: left of the action bar). */
		SLATE_ATTRIBUTE(float, MaxWidth)
	SLATE_END_ARGS()

	~SMRChatLog();
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;

	void OpenInput();
	void CloseInput();
	bool IsTyping() const { return bTyping; }
	/** What's typed so far (a tell's name from the who list), the cursor at its end. */
	void SetInputText(const FString& Text);
	/** The tab shown: -1 All, else an EMRChatChannel. */
	void SetTab(int32 Tab);
	int32 GetTab() const { return Tab; }

	/** The panel shows: typing, or the cursor free and over it. */
	bool IsActive() const;
	/** The right edge was dragged to this width (its own units). */
	void DragWidth(float Width);

private:
	void Rebuild();
	void OnChat(const FMRChatLine& Line);
	void Submit();
	FLinearColor ColorFor(uint8 Kind) const;
	bool Shows(const FMRChatLine& Line) const;
	/** The line as rich text: each run with its colour and style. */
	FString Markup(const FMRChatLine& Line) const;
	/** How visible a line that came at this time is while idle (1 fresh, fading to 0). */
	float LineAlpha(double Time) const;
	float WidthNow() const;

	TWeakObjectPtr<UMRUISubsystem> UI;
	TAttribute<float> MaxWidth;
	TSharedPtr<SVerticalBox> Lines;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SEditableText> Input;
	FDelegateHandle ChatHandle;
	FTextBlockStyle BaseStyle;
	FTextBlockStyle BoldStyle;
	FSlateColorBrush InputBrush = FSlateColorBrush(FLinearColor::Black);
	FSlateColorBrush EdgeBrush = FSlateColorBrush(FLinearColor::Black);
	int32 Tab = -1;
	bool bTyping = false;
	/** The panel's fade (Shards: background 0.2 s). */
	float PanelAlpha = 0.f;
	/** Everything's opacity (0 while Hide Interface has it away). */
	float Opacity = 1.f;
	int32 ScrollFrames = 0;
	/** Lines typed before (Up and Down), newest last. */
	TArray<FString> History;
	int32 HistoryAt = 0;
};
