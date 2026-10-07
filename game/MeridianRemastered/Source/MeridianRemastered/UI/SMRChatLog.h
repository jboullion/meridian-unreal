#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMRTextField;
class SScrollBox;
class SVerticalBox;
class UMRUISubsystem;
struct FMRChatLine;

/**
 * The chat log (bottom left, online only): the last lines of speech and game messages from the
 * server (UMRNetSubsystem::OnChat), in an inset panel like the hotbar's. It fades a while after the last
 * line. Enter opens a line to type (UMRUISubsystem::OpenChat): Enter says it, Escape cancels.
 */
class MERIDIANREMASTERED_API SMRChatLog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRChatLog) {}
	SLATE_END_ARGS()

	~SMRChatLog();
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override;

	void OpenInput();
	void CloseInput();
	bool IsTyping() const { return bTyping; }

private:
	void Rebuild();
	void OnChat(const FMRChatLine& Line);
	void Submit();
	FLinearColor ColorFor(uint8 Kind) const;

	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SVerticalBox> Lines;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SMRTextField> Input;
	FDelegateHandle ChatHandle;
	bool bTyping = false;
	double LastLineTime = -100.0;
	float Opacity = 0.f;
	int32 ScrollFrames = 0;
};
