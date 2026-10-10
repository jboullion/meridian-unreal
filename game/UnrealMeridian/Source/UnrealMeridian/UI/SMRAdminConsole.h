#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UI/SMRSocial.h"

class SMRSelectList;
class SMRTextField;
class SMultiLineEditableText;

/**
 * The admin console (docs/admin-console.md): the Escape menu's "Admin Console", for characters the
 * server gave the admin or DM module (UMRNetSubsystem::IsStaff). After the original's admin window
 * (module/admin admindlg.c) and Meridian Shards' AdminConsole.tsx, with buttons for the common jobs:
 *
 * - Travel: every room (data/net/rooms.json) to go to, and where we are.
 * - Players: who is on: show, go to, bring here, rescue.
 * - Self: the DM say commands about ourselves (immortal, boost stats, spells, items, hiding, karma).
 * - World: the time of day, monsters, scenery, messages to everyone, the server's state.
 *
 * Under every page are the server's answers and a command line: a server command (BP_REQ_ADMIN, the
 * maintenance port's), or "dm ..." for a DM command (SAY_DM). Up and Down step through what was sent.
 * Everything changes the live world at once.
 */
class UNREALMERIDIAN_API SMRAdminConsole : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRAdminConsole) {}
	SLATE_END_ARGS()
	virtual ~SMRAdminConsole() override;
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
	virtual void OnOpened() override;
	virtual FReply OnPreviewKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;

	/** "Travel", "Players", "Self" or "World". */
	void SetTab(const FString& InTab);
	const FString& GetTab() const { return Tab; }
	/** A line as typed: "dm ..." a DM command, "clear" empties the answers, "quit" closes, else a server command. */
	void RunLine(const FString& Line);
	/** Teleport ourselves to a room by its Kod RID (the Travel page's Go there). */
	void GoToRoom(int32 Rid);

private:
	TSharedRef<SWidget> MakeTravel();
	TSharedRef<SWidget> MakePlayers();
	TSharedRef<SWidget> MakeSelf();
	TSharedRef<SWidget> MakeWorld();
	/** Send a server command (admins only) or a DM command, and remember it for Up and Down. */
	void Admin(const FString& Command);
	void DM(const FString& Text);
	void Remember(const FString& Line);
	/** The room list for what's typed in the search (a name, a room file or a number). */
	void FilterRooms();
	void RefreshUsers();
	void OnAdminText();
	/** Our own object (what "send object" needs: SELF only works as a parameter). */
	uint32 Me() const;
	/** The player picked on the Players page, or 0. */
	uint32 PickedUser() const;
	int32 PickedRoom() const;

	FString Tab = TEXT("Travel");
	FString Status;
	TArray<FString> History;
	int32 HistoryAt = 0;
	FDelegateHandle AdminTextHandle;
	TSharedPtr<SMRTextField> CommandField, RoomSearch, ItemField, DisguiseField, MonsterField, PlaceField, MessageField;
	TSharedPtr<SMRSelectList> RoomList, UserList;
	TSharedPtr<SMultiLineEditableText> Output;
	FTextBlockStyle OutputStyle;
	/** The rooms and players in the lists, row by row. */
	TArray<int32> ShownRooms;
	TArray<uint32> ShownUsers;
	/** Clear Inventory asks to be pressed twice. */
	bool bConfirmClear = false;
};
