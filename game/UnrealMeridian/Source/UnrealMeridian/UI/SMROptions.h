#pragma once

#include "CoreMinimal.h"
#include "UI/SMRSocial.h"

class SMRTextField;

/**
 * The Options window (docs/adr/0009-user-interface.md, "Options"; the Escape menu's Options, or the
 * "password" and "suicide" commands): graphics (the engine's UGameUserSettings), sound, the keys
 * (MRKeys, Modern and Original presets), the game (the server-kept options, damage numbers, the
 * original's typing), the chat (the quick chat's function keys) and the account (the password,
 * deleting the character). What changes is kept at once (GameUserSettings.ini, the character's file).
 */
class UNREALMERIDIAN_API SMROptionsDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMROptionsDialog) {}
	SLATE_END_ARGS()
	virtual ~SMROptionsDialog() override;
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
	virtual void OnOpened() override;
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

	/** "Graphics", "Sound", "Controls", "Game", "Chat" or "Account". */
	void SetTab(const FString& InTab);
	const FString& GetTab() const { return Tab; }

private:
	TSharedRef<SWidget> MakeGraphics();
	TSharedRef<SWidget> MakeSound();
	TSharedRef<SWidget> MakeControls();
	TSharedRef<SWidget> MakeGame();
	TSharedRef<SWidget> MakeChat();
	TSharedRef<SWidget> MakeAccount();
	/** A key or mouse button for the binding being set (Controls). */
	void Capture(const FKey& Key);
	void OnPassword(bool bOk);

	FString Tab = TEXT("Graphics");
	FName Capturing;
	FString Status;
	FDelegateHandle PasswordHandle, PrefsHandle;
	TSharedPtr<SMRTextField> OldPassword, NewPassword, NewPassword2, DeletePassword;
	TArray<TSharedPtr<SMRTextField>> QuickFields;
};
