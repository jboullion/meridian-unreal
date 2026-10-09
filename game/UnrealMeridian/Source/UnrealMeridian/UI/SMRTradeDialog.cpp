#include "UI/SMRTradeDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRTrade"

namespace
{
	constexpr float WidthPx = 210.f;
	constexpr float ListPx = 120.f;

	UMRNetSubsystem* NetOf(const UMRUISubsystem* Ui)
	{
		const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
		const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	}

	TArray<UMRNetSubsystem::FItemCount> ToNet(const TArray<FMRItemCountView>& Items)
	{
		TArray<UMRNetSubsystem::FItemCount> Out;
		for (const FMRItemCountView& I : Items)
		{
			Out.Add({I.Id, I.Amount});
		}
		return Out;
	}
}

void SMRTradeDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
}

void SMRTradeDialog::RowsFrom(const TArray<FMRNetObject>& Objects, TArray<FRow>& Out)
{
	Out.Reset();
	for (const FMRNetObject& O : Objects)
	{
		FRow& R = Out.AddDefaulted_GetRef();
		R.Id = O.Id;
		R.Name = O.Name;
		R.Icon = O.Icon;
		R.bNumber = O.bNumber;
		R.Max = O.bNumber ? FMath::Max(1u, O.Amount) : 1;
	}
}

void SMRTradeDialog::CarriedRows(TArray<FRow>& Out) const
{
	const UMRNetSubsystem* Net = NetOf(UI.Get());
	TArray<FMRNetObject> Carried;
	for (const FMRNetObject& O : Net ? Net->GetInventory() : TArray<FMRNetObject>())
	{
		if (!Net->IsUsing(O.Id))
		{
			Carried.Add(O);  // (what is worn or wielded is taken off first, as the original)
		}
	}
	RowsFrom(Carried, Out);
}

uint32 SMRTradeDialog::AmountOf(const FRow& Row) const
{
	if (!Row.bNumber)
	{
		return 1;
	}
	const int32 V = Row.Amount.IsValid() ? FCString::Atoi(*Row.Amount->GetText()) : 1;
	return static_cast<uint32>(FMath::Clamp(V, 1, static_cast<int32>(FMath::Min<uint32>(Row.Max, INT32_MAX))));
}

TArray<FMRItemCountView> SMRTradeDialog::Chosen(const TArray<FRow>& InRows) const
{
	TArray<FMRItemCountView> Out;
	for (const FRow& R : InRows)
	{
		if (R.bSelected)
		{
			Out.Add({R.Id, AmountOf(R)});
		}
	}
	return Out;
}

TSharedRef<SWidget> SMRTradeDialog::MakeList(TArray<FRow>& InRows, bool bPick)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (InRows.Num() == 0)
	{
		List->AddSlot().AutoHeight().Padding(4.f * Px)[MRUI::Label(S, LOCTEXT("Nothing", "Nothing"), 9.f, false, FLinearColor(0.7f, 0.68f, 0.62f))];
	}
	for (int32 i = 0; i < InRows.Num(); ++i)
	{
		FRow& R = InRows[i];
		const FString Icon = R.Icon;
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f * Px, 0.f)
			[
				SNew(SBox).WidthOverride(14.f * Px).HeightOverride(14.f * Px)
				[
					SNew(SImage).Image_Lambda([Ui, Icon]() { return Ui->ItemIcon(Icon); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MRUI::Label(S, FText::FromString(R.bNumber && !bPick ? FString::Printf(TEXT("%u %s"), R.Max, *R.Name) : R.Name), 9.5f)
			];
		if (R.Price > 0 || Mode == EMRTradeMode::Shop)
		{
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3.f * Px, 0.f)
			[
				MRUI::Label(S, FText::Format(LOCTEXT("Price", "{0}"), R.Price), 9.5f, true, FLinearColor(1.f, 0.85f, 0.4f))
			];
		}
		if (bPick && R.bNumber)
		{
			// how many (a carried stack: up to all of it; a shop's: as many as wanted)
			R.Amount = SNew(SMRTextField, Ui).Width(26.f).MaxLength(6)
				.InitialText(FText::AsNumber(Mode == EMRTradeMode::Shop ? 1 : R.Max, &FNumberFormattingOptions::DefaultNoGrouping()));
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[R.Amount.ToSharedRef()];
		}
		List->AddSlot().AutoHeight().Padding(1.f * Px, 0.5f * Px)
		[
			SNew(SBorder).BorderImage(S->White()).Padding(FMargin(2.f * Px, 1.f * Px))
			.BorderBackgroundColor_Lambda([this, &InRows, i]()
			{
				return InRows.IsValidIndex(i) && InRows[i].bSelected ? FLinearColor(1.f, 0.8f, 0.35f, 0.35f) : FLinearColor(0.f, 0.f, 0.f, 0.15f);
			})
			.OnMouseButtonDown_Lambda([this, &InRows, i, bPick](const FGeometry&, const FPointerEvent&)
			{
				if (bPick && InRows.IsValidIndex(i))
				{
					InRows[i].bSelected = !InRows[i].bSelected;
				}
				return FReply::Handled();
			})
			[
				Line
			]
		];
	}
	return SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(ListPx * Px)
		[
			SNew(SScrollBox).ScrollBarThickness(FVector2D(5.f * Px, 5.f * Px))
			+ SScrollBox::Slot()[List]
		]
	];
}

void SMRTradeDialog::Rebuild(const FText& Title, TSharedRef<SWidget> Body, TSharedPtr<SWidget> Main, const FText& CloseText)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return;
	}
	const float Px = S->Px();
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SMRPanel, Ui).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SBox).WidthOverride(WidthPx * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
				[
					MRUI::Label(S, Title, 12.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight()[Body]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 4.f * Px, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f * Px, 0.f)
					[
						Main.IsValid() ? Main.ToSharedRef() : SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMRTextButton, Ui).Text(CloseText).TextSize(10.f).MinWidth(50.f)
							.OnClicked(FSimpleDelegate::CreateSP(this, &SMRTradeDialog::Close))
					]
				]
			]
		]
	];
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMRTradeDialog::Close()
{
	UMRUISubsystem* Ui = UI.Get();
	if (Mode == EMRTradeMode::Trade)
	{
		if (UMRNetSubsystem* Net = NetOf(Ui))
		{
			Net->CancelOffer();  // the offer is off for both sides (BP_CANCEL_OFFER)
		}
	}
	Mode = EMRTradeMode::None;
	if (Ui)
	{
		Ui->CloseTrade();
	}
}

FReply SMRTradeDialog::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geo, Event);
}

void SMRTradeDialog::ShowPick(uint32 InToId, const FString& ToName, const FText& Verb, bool bInDeposit)
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui)
	{
		return;
	}
	Mode = EMRTradeMode::Pick;
	ToId = InToId;
	bDeposit = bInDeposit;
	CarriedRows(Rows);
	const FText Title = FText::Format(LOCTEXT("PickTitle", "{0}: {1}"), Verb, FText::FromString(ToName));
	TSharedRef<SWidget> Main = SNew(SMRTextButton, Ui).Text(Verb).TextSize(10.f).MinWidth(50.f)
		.OnClicked(FSimpleDelegate::CreateSP(this, &SMRTradeDialog::Confirm));
	Rebuild(Title, MakeList(Rows, true), Main, LOCTEXT("Close", "Close"));
}

void SMRTradeDialog::ShowShop()
{
	UMRUISubsystem* Ui = UI.Get();
	const UMRNetSubsystem* Net = NetOf(Ui);
	if (!Ui || !Net)
	{
		return;
	}
	const FMRNetShop& Shop = Net->GetShop();
	Mode = EMRTradeMode::Shop;
	Rows.Reset();
	for (const FMRNetForSale& E : Shop.Items)
	{
		FRow& R = Rows.AddDefaulted_GetRef();
		R.Id = E.Object.Id;
		R.Name = E.Object.Name;
		R.Icon = E.Object.Icon;
		R.bNumber = E.Object.bNumber;
		R.Max = E.Object.bNumber ? 9999 : 1;
		R.Price = E.Price;
	}
	const FText Verb = Shop.bWithdrawal ? LOCTEXT("Withdraw", "Withdraw") : LOCTEXT("Buy", "Buy");
	UMRUIStyle* S = Ui->GetStyle();
	// the cost of what is chosen (buy.c: a running total)
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeList(Rows, true)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f * S->Px(), 0.f, 0.f)
		[
			MRUI::Label(S, TAttribute<FText>::CreateLambda([this]()
			{
				uint64 Total = 0;
				for (const FRow& R : Rows)
				{
					Total += R.bSelected ? uint64(R.Price) * AmountOf(R) : 0;
				}
				return FText::Format(LOCTEXT("Total", "Total: {0} shillings"), Total);
			}), 9.5f, true)
		];
	TSharedRef<SWidget> Main = SNew(SMRTextButton, Ui).Text(Verb).TextSize(10.f).MinWidth(50.f)
		.OnClicked(FSimpleDelegate::CreateSP(this, &SMRTradeDialog::Confirm));
	Rebuild(FText::Format(LOCTEXT("ShopTitle", "{0}: {1}"), Verb, FText::FromString(Shop.Seller.Name)), Body, Main, LOCTEXT("Close", "Close"));
}

void SMRTradeDialog::ShowTrade()
{
	UMRUISubsystem* Ui = UI.Get();
	const UMRNetSubsystem* Net = NetOf(Ui);
	if (!Ui || !Net)
	{
		return;
	}
	const FMRNetTrade& T = Net->GetTrade();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	Mode = EMRTradeMode::Trade;
	const FLinearColor Gold = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	const FText Them = FText::FromString(T.WithName.IsEmpty() ? TEXT("They") : T.WithName);
	// what they give: read only
	RowsFrom(T.Received, TheirRows);
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	TSharedPtr<SWidget> Main;
	if (!T.bOurs && !T.bCountered)
	{
		// offered to us: what they give, and what we give back (offer.c: a counteroffer, nothing at all is fine)
		CarriedRows(Rows);
		Body->AddSlot().AutoHeight()[MRUI::Label(S, FText::Format(LOCTEXT("TheyOffer", "{0} offers"), Them), 9.5f, true, Gold)];
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 3.f * Px)[MakeList(TheirRows, false)];
		Body->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("YouGiveBack", "You give in return"), 9.5f, true, Gold)];
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 0.f)[MakeList(Rows, true)];
		Main = SNew(SMRTextButton, Ui).Text(LOCTEXT("Answer", "Answer")).TextSize(10.f).MinWidth(50.f)
			.OnClicked(FSimpleDelegate::CreateSP(this, &SMRTradeDialog::Confirm));
	}
	else
	{
		RowsFrom(T.Given, GivenRows);
		Rows.Reset();
		Body->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("YouGive", "You give"), 9.5f, true, Gold)];
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 3.f * Px)[MakeList(GivenRows, false)];
		Body->AddSlot().AutoHeight()[MRUI::Label(S, FText::Format(LOCTEXT("TheyGive", "{0} gives"), Them), 9.5f, true, Gold)];
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 0.f)[MakeList(TheirRows, false)];
		if (T.bOurs && T.bAnswered)
		{
			Main = SNew(SMRTextButton, Ui).Text(LOCTEXT("Accept", "Accept")).TextSize(10.f).MinWidth(50.f)
				.OnClicked(FSimpleDelegate::CreateSP(this, &SMRTradeDialog::Confirm));
		}
		else
		{
			Main = MRUI::Label(S, FText::Format(LOCTEXT("Waiting", "Waiting for {0}..."), Them), 9.f, false, FLinearColor(0.75f, 0.73f, 0.68f));
		}
	}
	Rebuild(FText::Format(LOCTEXT("TradeTitle", "Trading with {0}"), Them), Body, Main, LOCTEXT("Cancel", "Cancel"));
}

void SMRTradeDialog::ShowBank(const FString& BankerName)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* Net = NetOf(Ui);
	if (!Ui || !Net)
	{
		return;
	}
	Mode = EMRTradeMode::Bank;
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	BankAmount = SNew(SMRTextField, Ui).Width(60.f).MaxLength(9).HintText(LOCTEXT("Amount", "Shillings"));
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(Net);
	const auto Amount = [this]() { return BankAmount.IsValid() ? FCString::Atoi(*BankAmount->GetText()) : 0; };
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			MRUI::Label(S, LOCTEXT("BankHelp", "Put shillings in or take them out; the banker answers aloud."), 8.f, false, FLinearColor(0.75f, 0.73f, 0.68f))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f * Px, 0.f)[BankAmount.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f * Px, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(LOCTEXT("Deposit", "Deposit")).TextSize(10.f).MinWidth(44.f)
					.OnClicked(FSimpleDelegate::CreateLambda([WeakNet, Amount]() { if (WeakNet.IsValid()) WeakNet->BankDeposit(Amount()); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f * Px, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(LOCTEXT("WithdrawMoney", "Withdraw")).TextSize(10.f).MinWidth(44.f)
					.OnClicked(FSimpleDelegate::CreateLambda([WeakNet, Amount]() { if (WeakNet.IsValid()) WeakNet->BankWithdraw(Amount()); }))
			]
		];
	TSharedRef<SWidget> Main = SNew(SMRTextButton, Ui).Text(LOCTEXT("Balance", "Balance")).TextSize(10.f).MinWidth(50.f)
		.OnClicked(FSimpleDelegate::CreateLambda([WeakNet]() { if (WeakNet.IsValid()) WeakNet->BankBalance(); }));
	Rebuild(FText::Format(LOCTEXT("BankTitle", "Bank: {0}"), FText::FromString(BankerName)), Body, Main, LOCTEXT("Close", "Close"));
}

bool SMRTradeDialog::Select(uint32 Id, uint32 Amount)
{
	for (FRow& R : Rows)
	{
		if (R.Id == Id)
		{
			R.bSelected = true;
			if (R.Amount.IsValid())
			{
				R.Amount->SetText(FString::FromInt(static_cast<int32>(Amount)));
			}
			return true;
		}
	}
	return false;
}

void SMRTradeDialog::Confirm()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetSubsystem* Net = NetOf(Ui);
	if (!Net)
	{
		return;
	}
	const TArray<UMRNetSubsystem::FItemCount> Items = ToNet(Chosen(Rows));
	switch (Mode)
	{
	case EMRTradeMode::Pick:
		if (Items.Num() == 0)
		{
			return;
		}
		if (bDeposit)
		{
			Net->Deposit(ToId, Items);
		}
		else
		{
			Net->Offer(ToId, Items);
		}
		Mode = EMRTradeMode::None;
		Ui->CloseTrade();  // the server's answer (BP_OFFERED, BP_COUNTEROFFER) opens the offer
		break;
	case EMRTradeMode::Shop:
		if (Items.Num() > 0)
		{
			Net->BuyItems(Items);
			Mode = EMRTradeMode::None;
			Ui->CloseTrade();
		}
		break;
	case EMRTradeMode::Trade:
		if (Net->GetTrade().bOurs)
		{
			Net->AcceptOffer();
			Mode = EMRTradeMode::None;
			Ui->CloseTrade();
		}
		else
		{
			Net->Counteroffer(Items);  // BP_COUNTEROFFERED shows it back
		}
		break;
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
