#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMRSelectList;
class SMRTextBox;
class SMRTextField;
class SVerticalBox;
class UMRNetSubsystem;
class UMRUISubsystem;
enum class EMRWindow : uint8;

/**
 * The windows for the others (docs/adr/0012 M8): who is on, mail, news boards, the guild. Each is a
 * stone panel in the middle of the screen; Esc or Close shuts it (UMRUISubsystem::SetWindowOpen).
 * They rebuild themselves from the session when it changes.
 */
class MERIDIANREMASTERED_API SMRSocialWindow : public SCompoundWidget
{
public:
	virtual ~SMRSocialWindow() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	/** Opened: start from the session's state. */
	virtual void OnOpened() { Rebuild(); }
	virtual void Rebuild() = 0;

protected:
	void Init(UMRUISubsystem* InUI, EMRWindow InKind);
	/** The panel around the content, with its title and the width in original pixels. */
	void SetFrame(const FText& Title, const TSharedRef<SWidget>& Content, float WidthPx);
	void Close();
	UMRNetSubsystem* Net() const;
	/** A local date and time for a Unix time ("Oct 08 14:05"). */
	static FString DateText(int64 UnixSeconds);

	TWeakObjectPtr<UMRUISubsystem> UI;
	EMRWindow Kind;
	FDelegateHandle UsersHandle, MailHandle, MailSentHandle, NewsHandle, GuildHandle;
};

/** Who is logged on (BP_PLAYERS; msgfiltr.c's who dialog): tell, ignore, and the chat filters. */
class MERIDIANREMASTERED_API SMRWhoDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRWhoDialog) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
};

/** Mail (module/mailnews): the kept messages, reading one, and writing one. */
class MERIDIANREMASTERED_API SMRMailDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRMailDialog) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
	virtual void OnOpened() override;
	/** Start a new message (To, Subject filled in for a reply). */
	void Compose(const FString& To = FString(), const FString& Subject = FString(), const FString& Body = FString());
	bool IsComposing() const { return bComposing; }
	/** Tests: send what's in the compose fields. */
	void SendComposed();

private:
	void OnSent(bool bOk, const FString& Why);
	bool bComposing = false;
	int32 Selected = INDEX_NONE;
	FString Status;
	TSharedPtr<SMRSelectList> List;
	TSharedPtr<SMRTextField> ToField, SubjectField;
	TSharedPtr<SMRTextBox> BodyBox;
};

/** A news board (BP_LOOK_NEWSGROUP): its articles, reading one, posting one. */
class MERIDIANREMASTERED_API SMRNewsDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRNewsDialog) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;

private:
	bool bComposing = false;
	TSharedPtr<SMRSelectList> List;
	TSharedPtr<SMRTextField> TitleField;
	TSharedPtr<SMRTextBox> BodyBox;
};

/**
 * The large map ("map", M): the whole room with the player's notes on it (the original's map
 * annotations), kept per character and room. A click on the map marks where the next note goes.
 */
class MERIDIANREMASTERED_API SMRMapWindow : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRMapWindow) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
	virtual void OnOpened() override;
	/** Add a note (tests): at Where (room cm), or at the player. */
	void AddNote(const FString& Text, TOptional<FVector2D> Where = TOptional<FVector2D>());

private:
	/** The room's notes (by its .roo), and its origin in the world. */
	TArray<struct FMRMapNote>* Notes() const;
	FVector Origin() const;
	TOptional<FVector2D> Marked;
	TSharedPtr<SMRTextField> NoteField;
};

/** The guild (UC_GUILDINFO, UC_GUILD_LIST), or founding one (UC_GUILD_ASK). */
class MERIDIANREMASTERED_API SMRGuildDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRGuildDialog) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;

private:
	TSharedRef<SWidget> MakeMembers();
	TSharedRef<SWidget> MakeGuilds();
	TSharedRef<SWidget> MakeCreate();
	bool bShowGuilds = false;
	int32 SelectedMember = INDEX_NONE;
	int32 SelectedGuild = INDEX_NONE;
	bool bSecret = false;
	TSharedPtr<SMRTextField> NameField;
	TArray<TSharedPtr<SMRTextField>> RankFields;
};
