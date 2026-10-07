#include "UI/SMRInventoryScreen.h"

#include "Abilities/MRAttributeSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "UI/MRGameData.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "UI/SMRSlot.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRInventory"

namespace
{
	constexpr int32 Columns = 9;
	constexpr int32 BagRowsVisible = 4;

	using MRUI::Label;

	/** Icon hints for empty equipment slots (data/ui/ui_style.json "slot_hints" can override). */
	FName HintFor(EMREquipSlot Slot)
	{
		switch (Slot)
		{
		case EMREquipSlot::Head: return TEXT("helm");
		case EMREquipSlot::Amulet: return TEXT("shcharm");
		case EMREquipSlot::Torso: return TEXT("chainamr");
		case EMREquipSlot::Hands: return TEXT("gauntlet");
		case EMREquipSlot::Legs: return TEXT("pantsa");
		case EMREquipSlot::Ring1:
		case EMREquipSlot::Ring2: return TEXT("ring1");
		case EMREquipSlot::LeftHand: return TEXT("metlshld");
		case EMREquipSlot::RightHand: return TEXT("sword");
		default: return NAME_None;
		}
	}

	const TCHAR* TabArt[] = {TEXT("invent"), TEXT("spell"), TEXT("skills"), TEXT("stats"), TEXT("quest")};
}

// ------------------------------------------------------------------------------ SMRAvatar

void SMRAvatar::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Size = InArgs._Size;
}

int32 SMRAvatar::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	if (!Style)
	{
		return Layer;
	}
	const FVector2f S(Geo.GetLocalSize());
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	// a dark niche, like Minecraft's black box behind the player
	MRPaint::Box(Out, Layer, Geo, Style->White(), FVector2f::ZeroVector, S, Tint * Style->Color(TEXT("avatar_bg"), FLinearColor(0.01f, 0.01f, 0.012f, 1.f)));
	if (UObject* Target = Ui->GetAvatarTarget())
	{
		if (Brush.GetResourceObject() != Target)
		{
			UMRUIStyle::SetImage(Brush, Target, S);
		}
		// the render target is square: fit its height and show the middle of it
		const float U0 = FMath::Clamp((1.f - S.X / FMath::Max(1.f, S.Y)) * 0.5f, 0.f, 0.5f);
		Brush.SetUVRegion(FBox2f(FVector2f(U0, 0.f), FVector2f(1.f - U0, 1.f)));
		MRPaint::Box(Out, Layer + 1, Geo, &Brush, FVector2f::ZeroVector, S, Tint);
	}
	MRPaint::Frame(Out, Layer + 2, Geo, Style->Frame(TEXT("inset")), FVector2f::ZeroVector, S, Tint);
	return Layer + 4;
}

FReply SMRAvatar::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	DragAccum = 0.f;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMRAvatar::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event)
{
	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

FReply SMRAvatar::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!HasMouseCapture() || !UI.IsValid())
	{
		return FReply::Unhandled();
	}
	// one of the original's eight angles per step of drag
	DragAccum += Event.GetCursorDelta().X;
	const float Step = Geo.GetLocalSize().X * 0.25f;
	while (FMath::Abs(DragAccum) >= Step)
	{
		const int32 Dir = DragAccum > 0.f ? 1 : -1;
		UI->TurnAvatar(Dir);
		DragAccum -= Dir * Step;
	}
	return FReply::Handled();
}

// ------------------------------------------------------------------------------ SMRTab

void SMRTab::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Art = InArgs._Art;
	bActive = InArgs._bActive;
	OnClicked = InArgs._OnClicked;
}

FVector2D SMRTab::ComputeDesiredSize(float) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	const FVector2f S = Style ? Style->PieceSize(FName(*FString::Printf(TEXT("tab_%s_up"), *Art.ToString()))) : FVector2f::ZeroVector;
	return S.IsNearlyZero() ? FVector2D(52.f, 40.f) : FVector2D(S.X + 8.f * Style->Px(), S.Y);  // stretched wider by the row
}

int32 SMRTab::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!Style)
	{
		return Layer;
	}
	// the original's stat button stretched: its left edge, the middle filler tiled, the right cap,
	// and the icon (with the button's top and bottom bevel) in the middle
	const bool bDown = bActive.Get();
	const TCHAR* State = bDown ? TEXT("down") : TEXT("up");
	const FVector2f S(Geo.GetLocalSize());
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FName LeftName(*FString::Printf(TEXT("tab_left_%s"), State)), MidName(*FString::Printf(TEXT("tab_mid_%s"), State));
	const FName RightName(*FString::Printf(TEXT("tab_right_%s"), State));
	const FName IconName(*FString::Printf(TEXT("tab_%s_icon_%s"), *Art.ToString(), State));
	// everything scaled to the row's height
	const float K = S.Y / FMath::Max(1.f, Style->PieceSize(MidName).Y);
	const FVector2f L = Style->PieceSize(LeftName) * K, R = Style->PieceSize(RightName) * K, I = Style->PieceSize(IconName) * K;
	MRPaint::Box(Out, Layer, Geo, Style->Brush(LeftName), FVector2f::ZeroVector, L, Tint);
	MRPaint::Tile(Out, Layer, Geo, Style->Brush(MidName, true, K), FVector2f(L.X, 0.f), FVector2f(S.X - L.X - R.X, S.Y), Tint);
	MRPaint::Box(Out, Layer, Geo, Style->Brush(RightName), FVector2f(S.X - R.X, 0.f), R, Tint);
	MRPaint::Box(Out, Layer + 1, Geo, Style->Brush(IconName), FVector2f((S.X - I.X) * 0.5f, 0.f), I, Tint);
	if (IsHovered() && !bDown)
	{
		MRPaint::Box(Out, Layer + 1, Geo, Style->White(), FVector2f::ZeroVector, S, FLinearColor(1.f, 1.f, 1.f, 0.12f));
	}
	return Layer + 2;
}

FReply SMRTab::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnClicked.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRInventoryScreen

void SMRInventoryScreen::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 i = 0; i < static_cast<int32>(EMRInventoryTab::Count); ++i)
	{
		const EMRInventoryTab T = static_cast<EMRInventoryTab>(i);
		// the five buttons share the row, icons centred (the page's name is the tooltip)
		Tabs->AddSlot().FillWidth(1.f).Padding(i > 0 ? 2.f * Px : 0.f, 0.f, 0.f, 0.f)
		[
			SNew(SMRTab, InUI).Art(TabArt[i]).ToolTipText(TitleOf(T))
				.bActive_Lambda([this, T]() { return Tab == T; })
				.OnClicked_Lambda([this, T]() { SetTab(T); })
		];
	}

	// every page stacked (the hidden ones still take space), so the window keeps one size across tabs
	TSharedRef<SOverlay> Stack = SNew(SOverlay);
	const TSharedRef<SWidget> PageWidgets[] = {MakeInventoryPage(), MakeSpellsPage(), MakeSkillsPage(), MakeStatsPage(), MakeQuestsPage()};
	for (int32 i = 0; i < UE_ARRAY_COUNT(PageWidgets); ++i)
	{
		const EMRInventoryTab T = static_cast<EMRInventoryTab>(i);
		// the other pages fill the space the inventory page takes (their windows span the dialog)
		Stack->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SBox).Visibility_Lambda([this, T]() { return Tab == T ? EVisibility::Visible : EVisibility::Hidden; })
			[
				PageWidgets[i]
			]
		];
	}
	Pages = Stack;

	Window = SNew(SMRPanel, InUI).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f * Px)
		[
			Tabs
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Pages.ToSharedRef()
		]
	];

	ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		Window.ToSharedRef()
	];
	RebuildBag();
	RebuildSpells();
	RebuildSkills();
}

FText SMRInventoryScreen::TabTitle() const
{
	return TitleOf(Tab);
}

FText SMRInventoryScreen::TitleOf(EMRInventoryTab InTab)
{
	switch (InTab)
	{
	case EMRInventoryTab::Inventory: return LOCTEXT("Inventory", "Inventory");
	case EMRInventoryTab::Spells: return LOCTEXT("Spells", "Spells");
	case EMRInventoryTab::Skills: return LOCTEXT("Skills", "Skills");
	case EMRInventoryTab::Stats: return LOCTEXT("Stats", "Statistics");
	case EMRInventoryTab::Quests: return LOCTEXT("Quests", "Quests");
	default: return FText::GetEmpty();
	}
}

TSharedRef<SWidget> SMRInventoryScreen::MakeInventoryPage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	const float Scroll = S->Number(TEXT("scrollbar_px"), 6.f) * Px;

	auto Equip = [Ui, SlotPx](EMREquipSlot E) -> TSharedRef<SWidget>
	{
		return SNew(SMRSlot, Ui, FMRSlotRef::Equip(E)).Size(SlotPx).HintIcon(HintFor(E));
	};
	auto Column = [&](std::initializer_list<EMREquipSlot> Slots) -> TSharedRef<SWidget>
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (EMREquipSlot E : Slots)
		{
			Box->AddSlot().AutoHeight()[Equip(E)];
		}
		return Box;
	};

	auto Totals = [Ui](bool bWeight)
	{
		return TAttribute<FText>::CreateLambda([Ui, bWeight]()
		{
			int32 W = 0, B = 0;
			if (Ui && Ui->GetSource())
			{
				Ui->GetSource()->GetTotals(W, B);
			}
			return bWeight ? FText::Format(LOCTEXT("Weight", "Weight  {0}"), FText::AsNumber(W))
				: FText::Format(LOCTEXT("Bulk", "Bulk  {0}"), FText::AsNumber(B));
		});
	};
	auto Held = TAttribute<FText>::CreateLambda([Ui]()
	{
		UMRInventorySource* Src = Ui ? Ui->GetSource() : nullptr;
		if (!Src)
		{
			return FText::GetEmpty();
		}
		const FMRSlotContent C = Src->Get(FMRSlotRef::Equip(EMREquipSlot::RightHand));
		return C.IsEmpty() ? LOCTEXT("Bare", "Bare hands") : Ui->NameFor(C);
	});

	BagGrid = SNew(SUniformGridPanel);
	TSharedRef<SHorizontalBox> HotbarRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < Columns; ++i)
	{
		HotbarRow->AddSlot().AutoWidth()
		[
			SNew(SMRSlot, Ui, FMRSlotRef(EMRSlotArea::Hotbar, i)).Size(SlotPx).KeyLabel(FString::FromInt(i + 1)).bSelectable(true)
		];
	}

	const float AvatarW = SlotPx * 3.f, AvatarH = SlotPx * 5.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Column({EMREquipSlot::Head, EMREquipSlot::Amulet, EMREquipSlot::Torso, EMREquipSlot::Hands, EMREquipSlot::Legs})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f)
			[
				SNew(SMRAvatar, Ui).Size(FVector2D(AvatarW * Px, AvatarH * Px))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Column({EMREquipSlot::Ring1, EMREquipSlot::Ring2, EMREquipSlot::LeftHand, EMREquipSlot::RightHand})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f * Px, 2.f * Px, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(S, LOCTEXT("Wielding", "Wielding"), 8.f, false, FLinearColor(0.8f, 0.78f, 0.7f))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f * Px)[Label(S, Held, 10.f, true)]
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Totals(true), 9.f)]
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Totals(false), 9.f)]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)
		[
			SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(0.f)
			[
				SNew(SBox).HeightOverride(SlotPx * BagRowsVisible * Px).WidthOverride(SlotPx * Columns * Px + Scroll)
				[
					SNew(SScrollBox).ScrollBarThickness(FVector2D(Scroll, Scroll)).ScrollBarPadding(0.f)
					+ SScrollBox::Slot()
					[
						BagGrid.ToSharedRef()
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)
		[
			SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[HotbarRow]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SSpacer).Size(FVector2D(Scroll, 1.f))]
			]
		];
}

void SMRInventoryScreen::RebuildBag()
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui || !Ui->GetSource() || !BagGrid.IsValid())
	{
		return;
	}
	const int32 N = Ui->GetSource()->NumSlots(EMRSlotArea::Bag);
	if (N == BagSlotsShown)
	{
		return;
	}
	BagSlotsShown = N;
	BagGrid->ClearChildren();
	const float SlotPx = Ui->GetStyle()->Number(TEXT("slot_px"), 22.f);
	for (int32 i = 0; i < N; ++i)
	{
		BagGrid->AddSlot(i % Columns, i / Columns)
		[
			SNew(SMRSlot, Ui, FMRSlotRef(EMRSlotArea::Bag, i)).Size(SlotPx)
		];
	}
}

TSharedRef<SWidget> SMRInventoryScreen::MakeSpellsPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	SpellList = SNew(SVerticalBox);
	return SNew(SMRPanel, UI.Get()).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px)
		[
			SNew(SScrollBox).ScrollBarThickness(FVector2D(6.f * Px, 6.f * Px))
			+ SScrollBox::Slot()[SpellList.ToSharedRef()]
		]
	];
}

void SMRInventoryScreen::RebuildSpells()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRInventorySource* Src = Ui ? Ui->GetSource() : nullptr;
	UMRGameDataSubsystem* Data = Ui ? Ui->GetData() : nullptr;
	if (!Src || !Data || !SpellList.IsValid())
	{
		return;
	}
	const TArray<FName>& Known = Src->GetKnownSpells();
	if (Known.Num() == SpellsShown)
	{
		return;
	}
	SpellsShown = Known.Num();
	SpellList->ClearChildren();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("list_slot_px"), 18.f);

	// by school, then level, then name (the original's spell list is alphabetical; schools read better)
	TArray<int32> Order;
	for (int32 i = 0; i < Known.Num(); ++i)
	{
		if (Data->FindSpell(Known[i]))
		{
			Order.Add(i);
		}
	}
	Order.Sort([&](int32 A, int32 B)
	{
		const FMRSpellDef* SA = Data->FindSpell(Known[A]);
		const FMRSpellDef* SB = Data->FindSpell(Known[B]);
		if (SA->School != SB->School) return SA->School < SB->School;
		if (SA->Level != SB->Level) return SA->Level < SB->Level;
		return SA->Name.CompareTo(SB->Name) < 0;
	});
	int32 School = -1;
	for (int32 i : Order)
	{
		const FMRSpellDef* Def = Data->FindSpell(Known[i]);
		if (Def->School != School)
		{
			School = Def->School;
			SpellList->AddSlot().AutoHeight().Padding(2.f * Px, 4.f * Px, 0.f, 1.f * Px)
			[
				Label(S, UMRGameDataSubsystem::SchoolName(School), 10.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
			];
		}
		FString Reagents;
		for (const FMRReagent& R : Def->Reagents)
		{
			const FMRItemDef* Item = Data->FindItem(R.Item);
			Reagents += FString::Printf(TEXT("%s%d %s"), Reagents.IsEmpty() ? TEXT("") : TEXT(", "), R.Count,
				Item ? *Item->Name.ToString() : *R.Item.ToString());
		}
		const FText Info = FText::Format(LOCTEXT("SpellInfo", "Level {0}  ·  {1} mana{2}"), FText::AsNumber(Def->Level), FText::AsNumber(Def->Mana),
			Reagents.IsEmpty() ? FText::GetEmpty() : FText::FromString(TEXT("  ·  ") + Reagents));
		// the whole row shows the spell's tooltip, not only its icon
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->SetToolTip(Ui->MakeToolTip(FMRSlotContent::Spell(Known[i])));
		SpellList->AddSlot().AutoHeight().Padding(2.f * Px, 1.f * Px)
		[
			Row
		];
		Row->AddSlot().AutoWidth()
			[
				SNew(SMRSlot, Ui, FMRSlotRef(EMRSlotArea::SpellBook, i)).Size(SlotPx)
			];
		Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f * Px, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Def->Name, 10.f, true)]
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Info, 7.5f, false, FLinearColor(0.78f, 0.76f, 0.7f))]
			];
	}
	SpellList->AddSlot().AutoHeight().Padding(2.f * Px, 6.f * Px, 2.f * Px, 2.f * Px)
	[
		Label(S, LOCTEXT("SpellHelp", "Click a spell and put it on the spell bar, or shift-click it. Numpad 1-9 casts."), 7.5f, false,
			FLinearColor(0.7f, 0.68f, 0.62f))
	];
}

TSharedRef<SWidget> SMRInventoryScreen::MakeSkillsPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	SkillList = SNew(SVerticalBox);
	return SNew(SMRPanel, UI.Get()).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px)
		[
			SNew(SScrollBox).ScrollBarThickness(FVector2D(6.f * Px, 6.f * Px))
			+ SScrollBox::Slot()[SkillList.ToSharedRef()]
		]
	];
}

void SMRInventoryScreen::RebuildSkills()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRInventorySource* Src = Ui ? Ui->GetSource() : nullptr;
	UMRGameDataSubsystem* Data = Ui ? Ui->GetData() : nullptr;
	if (!Src || !Data || !SkillList.IsValid())
	{
		return;
	}
	const TMap<FName, int32>& Skills = Src->GetSkills();
	if (Skills.Num() == SkillsShown)
	{
		return;
	}
	SkillsShown = Skills.Num();
	SkillList->ClearChildren();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float IconPx = S->Number(TEXT("list_slot_px"), 18.f);

	TArray<FName> Names;
	Skills.GetKeys(Names);
	Names.Sort([Data](const FName& A, const FName& B)
	{
		const FMRSkillDef* SA = Data->FindSkill(A);
		const FMRSkillDef* SB = Data->FindSkill(B);
		return (SA ? SA->Name.ToString() : A.ToString()) < (SB ? SB->Name.ToString() : B.ToString());
	});
	for (const FName& Name : Names)
	{
		const FMRSkillDef* Def = Data->FindSkill(Name);
		const int32 Pct = Skills[Name];
		const FSlateBrush* Icon = Def ? S->Icon(Def->Icon) : nullptr;
		SkillList->AddSlot().AutoHeight().Padding(2.f * Px, 1.5f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(IconPx * Px).HeightOverride(IconPx * Px)
				[
					SNew(SImage).Image(Icon)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f * Px, 0.f)
			[
				Label(S, Def ? Def->Name : FText::FromName(Name), 10.f, true)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f * Px, 0.f)
			[
				SNew(SMRBar, Ui).Width(60.f).Height(5.f).bShowText(false).Value(static_cast<float>(Pct)).Max(100.f)
					.Color(S->Color(TEXT("skill"), FLinearColor(0.85f, 0.6f, 0.05f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(24.f * Px).HAlign(HAlign_Right)
				[
					Label(S, FText::Format(LOCTEXT("Pct", "{0}%"), FText::AsNumber(Pct)), 10.f, true)
				]
			]
		];
	}
}

TSharedRef<SWidget> SMRInventoryScreen::MakeStatsPage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);

	auto Get = [Ui](float (UMRAttributeSet::*Fn)() const)
	{
		return TAttribute<float>::CreateLambda([Ui, Fn]()
		{
			const UMRAttributeSet* A = Ui ? Ui->GetAttributes() : nullptr;
			return A ? (A->*Fn)() : 0.f;
		});
	};
	auto StatText = [Ui](float (UMRAttributeSet::*Fn)() const)
	{
		return TAttribute<FText>::CreateLambda([Ui, Fn]()
		{
			const UMRAttributeSet* A = Ui ? Ui->GetAttributes() : nullptr;
			return A ? FText::AsNumber(FMath::RoundToInt((A->*Fn)())) : FText::FromString(TEXT("-"));
		});
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	struct FStat { FText Name; float (UMRAttributeSet::*Fn)() const; };
	const FStat Stats[] = {
		{LOCTEXT("Might", "Might"), &UMRAttributeSet::GetMight},
		{LOCTEXT("Intellect", "Intellect"), &UMRAttributeSet::GetIntellect},
		{LOCTEXT("Stamina", "Stamina"), &UMRAttributeSet::GetStamina},
		{LOCTEXT("Agility", "Agility"), &UMRAttributeSet::GetAgility},
		{LOCTEXT("Mysticism", "Mysticism"), &UMRAttributeSet::GetMysticism},
		{LOCTEXT("Aim", "Aim"), &UMRAttributeSet::GetAim},
	};
	Box->AddSlot().AutoHeight().Padding(2.f * Px, 2.f * Px)
	[
		Label(S, LOCTEXT("Attributes", "Attributes"), 10.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
	];
	for (const FStat& Stat : Stats)
	{
		Box->AddSlot().AutoHeight().Padding(6.f * Px, 1.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[Label(S, Stat.Name, 10.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f * Px, 0.f)
			[
				SNew(SMRBar, Ui).Width(80.f).Height(5.f).bShowText(false).Value(Get(Stat.Fn)).Max(50.f)
					.Color(S->Color(TEXT("stat"), FLinearColor(0.6f, 0.5f, 0.35f)))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(20.f * Px).HAlign(HAlign_Right)[Label(S, StatText(Stat.Fn), 10.f, true)]
			]
		];
	}
	Box->AddSlot().AutoHeight().Padding(2.f * Px, 8.f * Px, 2.f * Px, 2.f * Px)
	[
		Label(S, LOCTEXT("Pools", "Health, mana and vigor"), 10.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
	];
	struct FPool { float (UMRAttributeSet::*Value)() const; float (UMRAttributeSet::*Max)() const; const TCHAR* Color; FLinearColor Default; };
	const FPool Pools[] = {
		{&UMRAttributeSet::GetHealth, &UMRAttributeSet::GetMaxHealth, TEXT("health"), FLinearColor(0.75f, 0.06f, 0.06f)},
		{&UMRAttributeSet::GetMana, &UMRAttributeSet::GetMaxMana, TEXT("mana"), FLinearColor(0.08f, 0.2f, 0.85f)},
		{&UMRAttributeSet::GetVigor, &UMRAttributeSet::GetMaxVigor, TEXT("vigor"), FLinearColor(0.85f, 0.6f, 0.05f)},
	};
	for (const FPool& P : Pools)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f * Px)
		[
			SNew(SMRBar, Ui).Width(SlotPx * Columns - 20.f).Height(9.f).Value(Get(P.Value)).Max(Get(P.Max)).Color(S->Color(P.Color, P.Default))
		];
	}
	return SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px)
		[
			Box
		]
	];
}

TSharedRef<SWidget> SMRInventoryScreen::MakeQuestsPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	TSharedRef<STextBlock> Help = Label(S, LOCTEXT("QuestHelp", "Quests you take from the people of Meridian will be listed here."), 8.f, false,
		FLinearColor(0.75f, 0.73f, 0.68f));
	Help->SetAutoWrapText(true);
	Help->SetJustification(ETextJustify::Center);
	return SNew(SMRPanel, UI.Get()).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px).HAlign(HAlign_Fill).VAlign(VAlign_Center)
		.Padding(12.f * Px, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Label(S, LOCTEXT("NoQuests", "No quests yet"), 12.f, true)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 4.f * Px)
			[
				Help
			]
		]
	];
}

void SMRInventoryScreen::SetTab(EMRInventoryTab InTab)
{
	Tab = InTab;
}

void SMRInventoryScreen::OnOpened()
{
	RebuildBag();
	RebuildSpells();
	RebuildSkills();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMRInventoryScreen::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geo, Time, Dt);
	RebuildBag();  // grows a row when the last one fills
}

int32 SMRInventoryScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	// dim the world behind the window
	if (UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr)
	{
		MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()),
			S->Color(TEXT("dim"), FLinearColor(0.f, 0.f, 0.f, 0.35f)));
	}
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 1, WStyle, bParentEnabled);
}

FReply SMRInventoryScreen::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui)
	{
		return FReply::Unhandled();
	}
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::E || Key == EKeys::I)
	{
		Ui->SetInventoryOpen(false);
		return FReply::Handled();
	}
	// a number over a slot swaps it with that hotbar slot (Minecraft); otherwise the game selects
	const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Numbers); ++i)
	{
		if (Key == Numbers[i] && Ui->GetHoveredSlot().IsValid() && Ui->GetSource())
		{
			Ui->GetSource()->SwapWithHotbar(Ui->GetHoveredSlot(), i);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SMRInventoryScreen::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	UMRUISubsystem* Ui = UI.Get();
	if (Ui)
	{
		Ui->NoteMouse(Event.GetScreenSpacePosition());
	}
	if (Ui && Window.IsValid() && !Window->GetTickSpaceGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		Ui->OnClickOutside(Event.GetEffectingButton() == EKeys::RightMouseButton);
	}
	return FReply::Handled();  // never through to the game (no attacks while the window is open)
}

FReply SMRInventoryScreen::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (UI.IsValid())
	{
		UI->NoteMouse(Event.GetScreenSpacePosition());
	}
	return FReply::Unhandled();
}

FReply SMRInventoryScreen::OnMouseWheel(const FGeometry& Geo, const FPointerEvent& Event)
{
	return FReply::Handled();  // no hotbar scrolling or camera zoom behind the window
}

#undef LOCTEXT_NAMESPACE
