#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMRTextField;
class SVerticalBox;
class UMRNetSubsystem;
class UMRUISubsystem;

/**
 * The login screen for playing on a Meridian server (docs/adr/0010-meridian-servers.md), built
 * like the inventory dialog (SMRInventoryScreen): one stone window with the gold corners, inset
 * panels inside, the stat-button art for buttons. Its pages follow the session (UMRNetSubsystem):
 * - Login: the server, login name and password (an unknown name makes a new account).
 * - Connecting: what is happening, and Cancel.
 * - Characters: the account's characters and empty slots; Play, Log off.
 * - Create: a name and male or female for an empty slot (the rest is the server's default).
 */
class MERIDIANREMASTERED_API SMRLoginScreen : public SCompoundWidget
{
public:
	enum class EPage : uint8 { Login, Connecting, Characters, Create };

	SLATE_BEGIN_ARGS(SMRLoginScreen) {}
	SLATE_END_ARGS()

	~SMRLoginScreen();
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** Shown: focus the first empty field. */
	void OnShown();
	EPage GetPage() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	UMRNetSubsystem* GetNet() const;
	TSharedRef<SWidget> MakeLoginPage();
	TSharedRef<SWidget> MakeConnectingPage();
	TSharedRef<SWidget> MakeCharactersPage();
	TSharedRef<SWidget> MakeCreatePage();
	TSharedRef<SWidget> MakeButtons();
	void RebuildCharacters();
	EVisibility PageVisibility(EPage Page) const;
	/** The server's message of the day, empty when it has none. */
	FString GetMotd() const;
	bool HasMotd() const;

	void LogIn();
	void Play();
	void Create();
	void Back();
	void LogOff();
	void Quit();

	/** Characters in display order: named ones by name, then the empty slots. */
	struct FEntry
	{
		uint32 Id = 0;
		FString Name;
		bool bEmpty = false;
	};
	TArray<FEntry> Entries;
	int32 Selected = 0;
	int32 Server = 0;
	bool bCreating = false;
	bool bFemale = false;
	uint32 CreateSlot = 0;
	FString LocalError;

	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<SMRTextField> NameField;
	TSharedPtr<SMRTextField> PasswordField;
	TSharedPtr<SMRTextField> CharNameField;
	TSharedPtr<SVerticalBox> CharacterList;
	FDelegateHandle CharactersHandle;
	FDelegateHandle PhaseHandle;
};
