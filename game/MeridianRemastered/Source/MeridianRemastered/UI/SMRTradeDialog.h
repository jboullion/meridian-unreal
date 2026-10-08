#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMRTextField;
class SVerticalBox;
class UMRUISubsystem;
struct FMRNetObject;

/** An item and how many, as the dialog hands them over (UMRNetSubsystem::FItemCount). */
struct FMRItemCountView
{
	uint32 Id = 0;
	uint32 Amount = 0;
};

/** What the trade dialog shows. */
enum class EMRTradeMode : uint8
{
	None,
	Pick,   // choose carried items to offer, sell, give or deposit to someone
	Shop,   // a shop's list (buy) or a vault's (withdraw)
	Trade,  // an offer under way: what each side gives; accept, answer or cancel
	Bank,   // a banker: shillings in and out, the balance
};

/**
 * Trading with NPCs and players (docs/adr/0012 M6; the original's buy.c, offer.c and gameuser.c lists):
 * one stone panel in four modes. Rows are the items with their icon and name, a price in a shop, and
 * an amount for number items (shillings, reagents); a click selects one. What goes to the server is
 * in UMRNetSubsystem (BP_REQ_BUY_ITEMS, BP_REQ_OFFER, BP_REQ_DEPOSIT, BP_REQ_COUNTEROFFER,
 * BP_ACCEPT_OFFER, UC_DEPOSIT...); the server's answers (BP_BUY_LIST, BP_OFFER...) open and refresh it.
 */
class MERIDIANREMASTERED_API SMRTradeDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRTradeDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** Choose carried items for someone: bDeposit puts them in a vault (or shillings with a banker), else an offer. */
	void ShowPick(uint32 ToId, const FString& ToName, const FText& Verb, bool bDeposit);
	/** The last shop or vault list (UMRNetSubsystem::GetShop). */
	void ShowShop();
	/** The offer under way (UMRNetSubsystem::GetTrade). */
	void ShowTrade();
	/** A banker's counter. */
	void ShowBank(const FString& BankerName);
	EMRTradeMode GetMode() const { return Mode; }

	/** Tests: select a row by its object (an amount for a number item), and press the main button. */
	bool Select(uint32 Id, uint32 Amount = 1);
	void Confirm();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;

private:
	struct FRow
	{
		uint32 Id = 0;
		FString Name;
		FString Icon;
		bool bNumber = false;
		uint32 Max = 1;       // a number item: how many there are (a shop: no limit)
		uint32 Price = 0;     // a shop: each
		bool bSelected = false;
		TSharedPtr<SMRTextField> Amount;
	};
	/** A list of rows; bPick: they can be chosen. */
	TSharedRef<SWidget> MakeList(TArray<FRow>& InRows, bool bPick);
	static void RowsFrom(const TArray<FMRNetObject>& Objects, TArray<FRow>& Out);
	/** The carried items, as rows to choose from (not those in use). */
	void CarriedRows(TArray<FRow>& Out) const;
	/** The chosen rows as the server wants them. */
	TArray<FMRItemCountView> Chosen(const TArray<FRow>& InRows) const;
	uint32 AmountOf(const FRow& Row) const;
	void Rebuild(const FText& Title, TSharedRef<SWidget> Body, TSharedPtr<SWidget> Main, const FText& CloseText);
	void Close();

	TWeakObjectPtr<UMRUISubsystem> UI;
	EMRTradeMode Mode = EMRTradeMode::None;
	TArray<FRow> Rows;
	/** In an offer: what they give, and what we gave (read only). */
	TArray<FRow> TheirRows;
	TArray<FRow> GivenRows;
	uint32 ToId = 0;
	bool bDeposit = false;
	TSharedPtr<SMRTextField> BankAmount;
};
