#include "UI/SMRStatChange.h"

#include "Framework/Application/SlateApplication.h"
#include "Net/MRNetWorld.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRStatChange"

namespace
{
	// user.kod UserChangedStats: each stat 1..50
	constexpr int32 StatMin = 1;
	constexpr int32 StatMax = 50;
	const TCHAR* StatNames[6] = {TEXT("Might"), TEXT("Intellect"), TEXT("Stamina"), TEXT("Agility"), TEXT("Mysticism"), TEXT("Aim")};
}

void SMRStatChange::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Rebuild();
}

void SMRStatChange::Reset(const FMRNetStatChange& Offer)
{
	Total = 0;
	for (int32 i = 0; i < 6; ++i)
	{
		Values[i] = Offer.Stats[i];
		Total += Values[i];
	}
	Rebuild();
}

int32 SMRStatChange::PointsLeft() const
{
	int32 Sum = 0;
	for (const int32 V : Values)
	{
		Sum += V;
	}
	return Total - Sum;
}

void SMRStatChange::Step(int32 Stat, int32 Delta)
{
	const int32 Next = Values[Stat] + Delta;
	if (Next < StatMin || Next > StatMax || (Delta > 0 && PointsLeft() <= 0))
	{
		return;
	}
	Values[Stat] = Next;
}

void SMRStatChange::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return;
	}
	const float Px = S->Px();
	TWeakObjectPtr<UMRUISubsystem> Weak(Ui);
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 i = 0; i < 6; ++i)
	{
		Rows->AddSlot().AutoHeight().Padding(0.f, 1.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[MRUI::Label(S, FText::FromString(StatNames[i]), 10.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT("-"))).TextSize(10.f).MinWidth(16.f)
					.OnClicked(FSimpleDelegate::CreateSP(this, &SMRStatChange::Step, i, -1))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(26.f * Px).HAlign(HAlign_Center)
				[
					MRUI::Label(S, TAttribute<FText>::CreateLambda([this, i]() { return FText::AsNumber(Values[i]); }), 11.f, true)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT("+"))).TextSize(10.f).MinWidth(16.f)
					.OnClicked(FSimpleDelegate::CreateSP(this, &SMRStatChange::Step, i, 1))
			]
		];
	}
	TSharedRef<STextBlock> Help = MRUI::Label(S, LOCTEXT("Help", "Move points between your stats; each stays between 1 and 50. Your school levels stay as they are."),
		8.f, false, FLinearColor(0.75f, 0.73f, 0.68f));
	Help->SetAutoWrapText(true);
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SMRPanel, Ui).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SBox).WidthOverride(170.f * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
				[
					MRUI::Label(S, LOCTEXT("Title", "Retraining"), 12.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Help]
				+ SVerticalBox::Slot().AutoHeight()[Rows]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f * Px)
				[
					MRUI::Label(S, TAttribute<FText>::CreateLambda([this]()
					{
						return FText::Format(LOCTEXT("Left", "Points left: {0}"), PointsLeft());
					}), 10.f, true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 3.f * Px, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f * Px, 0.f)
					[
						SNew(SMRTextButton, Ui).Text(LOCTEXT("Change", "Change")).TextSize(10.f).MinWidth(50.f)
							.IsEnabled_Lambda([this]() { return PointsLeft() == 0; })
							.OnClicked(FSimpleDelegate::CreateLambda([this, Weak]()
							{
								if (Weak.IsValid())
								{
									Weak->SubmitStatChange(Values);
								}
							}))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMRTextButton, Ui).Text(LOCTEXT("Cancel", "Cancel")).TextSize(10.f).MinWidth(50.f)
							.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) Weak->CloseStatChange(); }))
					]
				]
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
