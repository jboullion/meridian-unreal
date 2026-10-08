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
	Source = InArgs._Source;
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
	if (UObject* Target = Source == EMRAvatarSource::Inventory ? Ui->GetAvatarTarget()
		: Ui->GetCreatorTarget(Source == EMRAvatarSource::CreatorPortrait))
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
	const float Step = Geo.GetLocalSize().X * 0.175f;
	while (FMath::Abs(DragAccum) >= Step)
	{
		const int32 Dir = DragAccum > 0.f ? 1 : -1;
		if (Source == EMRAvatarSource::Inventory)
		{
			UI->TurnAvatar(Dir);
		}
		else if (Source == EMRAvatarSource::CreatorPortrait)
		{
			UI->TurnCreatorPortrait(Dir);
		}
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

// ------------------------------------------------------------------------------ list pages

namespace
{
	/**
	 * One row of a list page: a single hover region (highlighted) with one tooltip for the whole
	 * row, so moving between the icon and the text keeps it open; a click anywhere on it acts on its slot.
	 */
	class SMRListRow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMRListRow) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI, const FMRSlotRef& InSlot)
		{
			UI = InUI;
			SlotRef = InSlot;
			ChildSlot[InArgs._Content.Widget];
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (S && IsHovered())
			{
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()), FLinearColor(1.f, 0.95f, 0.8f, 0.08f));
			}
			return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 1, WStyle, bParentEnabled);
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			// the row's text acts like its slot (pick a spell up, shift-click it onto the bar)
			if (UI.IsValid() && SlotRef.IsValid() && UI->IsInventoryOpen()
				&& (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::RightMouseButton))
			{
				UI->NoteMouse(Event.GetScreenSpacePosition());
				UI->OnSlotMouseDown(SlotRef, Event.GetEffectingButton() == EKeys::RightMouseButton, Event.IsShiftDown());
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
		FMRSlotRef SlotRef;
	};

	/** A section's header: a divider above, an arrow, the title and a count; a click folds the section. */
	class SMRSectionHeader : public SLeafWidget
	{
	public:
		DECLARE_DELEGATE(FOnToggle);
		SLATE_BEGIN_ARGS(SMRSectionHeader) : _bCollapsed(false), _Count(-1), _bDivider(true) {}
			SLATE_ARGUMENT(FText, Title)
			SLATE_ARGUMENT(bool, bCollapsed)
			SLATE_ARGUMENT(int32, Count)
			SLATE_ARGUMENT(bool, bDivider)
			SLATE_EVENT(FOnToggle, OnToggle)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
		{
			UI = InUI;
			Title = InArgs._Title.ToString();
			bCollapsed = InArgs._bCollapsed;
			Count = InArgs._Count;
			bDivider = InArgs._bDivider;
			OnToggle = InArgs._OnToggle;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			const float Px = S ? S->Px() : 2.f;
			const float TextH = S ? MRPaint::MeasureText(TEXT("Ag"), S->Font(10.f, true)).Y : 14.f;
			return FVector2D(40.f * Px, TextH + 6.f * Px);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (!S)
			{
				return Layer;
			}
			const float Px = S->Px();
			const FVector2f Size(Geo.GetLocalSize());
			const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
			const FLinearColor Heading = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)) * Tint;
			if (bDivider)
			{
				// a groove in the stone: a dark line over a light one
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f(0.f, Px), FVector2f(Size.X, Px * 0.5f), FLinearColor(0.f, 0.f, 0.f, 0.55f) * Tint);
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f(0.f, Px * 1.5f), FVector2f(Size.X, Px * 0.5f), FLinearColor(1.f, 1.f, 1.f, 0.18f) * Tint);
			}
			if (IsHovered())
			{
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f(0.f, 2.f * Px), FVector2f(Size.X, Size.Y - 2.f * Px), FLinearColor(1.f, 0.95f, 0.8f, 0.06f) * Tint);
			}
			// the arrow: right when folded, down when open (scanlines: a small filled triangle)
			const float A = 4.f * Px;
			const FVector2f C(3.f * Px + A * 0.5f, 2.f * Px + (Size.Y - 2.f * Px) * 0.5f);
			const int32 Lines = FMath::Max(2, FMath::RoundToInt(A));
			for (int32 i = 0; i < Lines; ++i)
			{
				const float T = i / float(Lines - 1);  // 0 at the base, 1 at the tip
				const float Half = A * 0.5f * (1.f - T);
				TArray<FVector2f> Pts;
				if (bCollapsed)
				{
					const float X = C.X - A * 0.5f + T * A;
					Pts = {FVector2f(X, C.Y - Half), FVector2f(X, C.Y + Half + 0.01f)};
				}
				else
				{
					const float Y = C.Y - A * 0.5f + T * A;
					Pts = {FVector2f(C.X - Half, Y), FVector2f(C.X + Half + 0.01f, Y)};
				}
				FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Heading, true, 1.2f);
			}
			const FSlateFontInfo Font = S->Font(10.f, true);
			const FVector2f M = MRPaint::MeasureText(Title, Font);
			const float TextY = 2.f * Px + (Size.Y - 2.f * Px - M.Y) * 0.5f;
			MRPaint::Text(Out, Layer + 1, Geo, Title, Font, FVector2f(3.f * Px + A + 4.f * Px, TextY), Heading, Px * 0.5f);
			if (Count >= 0)
			{
				const FString N = FString::FromInt(Count);
				const FSlateFontInfo Small = S->Font(8.f);
				const FVector2f NM = MRPaint::MeasureText(N, Small);
				MRPaint::Text(Out, Layer + 1, Geo, N, Small, FVector2f(Size.X - NM.X - 4.f * Px, 2.f * Px + (Size.Y - 2.f * Px - NM.Y) * 0.5f),
					FLinearColor(0.75f, 0.73f, 0.68f) * Tint, Px * 0.5f);
			}
			return Layer + 3;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				OnToggle.ExecuteIfBound();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
		FString Title;
		bool bCollapsed = false;
		int32 Count = -1;
		bool bDivider = true;
		FOnToggle OnToggle;
	};

	bool MatchesFilter(const FString& Filter, const FString& A, const FString& B = FString())
	{
		return Filter.IsEmpty() || A.Contains(Filter, ESearchCase::IgnoreCase) || (!B.IsEmpty() && B.Contains(Filter, ESearchCase::IgnoreCase));
	}
}

TSharedRef<SWidget> SMRInventoryScreen::MakeListPage(TSharedPtr<SMRTextField>& OutSearch, TSharedPtr<SVerticalBox>& OutList, const FText& Hint)
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	OutList = SNew(SVerticalBox);
	// the search bar at the top filters the list as you type (Escape clears it)
	TSharedPtr<SMRTextField>* SearchPtr = &OutSearch;
	OutSearch = SNew(SMRTextField, UI.Get()).HintText(Hint).Width(SlotPx * Columns - 4.f).MaxLength(40)
		.OnCancel_Lambda([this, SearchPtr]()
		{
			if (SearchPtr->IsValid())
			{
				(*SearchPtr)->SetText(FString());
			}
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
			}
		});
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			OutSearch.ToSharedRef()
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SMRPanel, UI.Get()).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
			[
				SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px)
				[
					SNew(SScrollBox).ScrollBarThickness(FVector2D(6.f * Px, 6.f * Px))
					+ SScrollBox::Slot()[OutList.ToSharedRef()]
				]
			]
		];
}

TSharedRef<SWidget> SMRInventoryScreen::MakeSpellsPage()
{
	return MakeListPage(SpellSearch, SpellList, LOCTEXT("SearchSpells", "Search spells..."));
}

TSharedRef<SWidget> SMRInventoryScreen::MakeSkillsPage()
{
	return MakeListPage(SkillSearch, SkillList, LOCTEXT("SearchSkills", "Search skills..."));
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
	const FString Filter = SpellSearch.IsValid() ? SpellSearch->GetText().TrimStartAndEnd() : FString();
	// rebuilt only when what it shows changes: the spells, their percentages, the search, the folds
	FString Key = Filter + TEXT("|");
	for (const FName& N : Known)
	{
		Key += FString::Printf(TEXT("%s:%d,"), *N.ToString(), Src->GetSpellPercent(N));
	}
	for (int32 School : CollapsedSpellSchools)
	{
		Key += FString::Printf(TEXT("c%d"), School);
	}
	if (Key == SpellsKey)
	{
		return;
	}
	SpellsKey = Key;
	SpellList->ClearChildren();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("list_slot_px"), 18.f);

	// by school, then level, then name; a search shows every school's matches, unfolded
	TArray<int32> Order;
	for (int32 i = 0; i < Known.Num(); ++i)
	{
		const FMRSpellDef* Def = Data->FindSpell(Known[i]);
		if (Def && MatchesFilter(Filter, Def->Name.ToString(), UMRGameDataSubsystem::SchoolName(Def->School).ToString()))
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
	TMap<int32, int32> PerSchool;
	for (int32 i : Order)
	{
		PerSchool.FindOrAdd(Data->FindSpell(Known[i])->School)++;
	}
	int32 School = -1;
	bool bFirst = true;
	for (int32 i : Order)
	{
		const FMRSpellDef* Def = Data->FindSpell(Known[i]);
		const bool bFolded = Filter.IsEmpty() && CollapsedSpellSchools.Contains(Def->School);
		if (Def->School != School)
		{
			School = Def->School;
			const int32 ThisSchool = School;
			SpellList->AddSlot().AutoHeight().Padding(0.f, bFirst ? 0.f : 2.f * Px, 0.f, 1.f * Px)
			[
				SNew(SMRSectionHeader, Ui).Title(UMRGameDataSubsystem::SchoolName(School)).bCollapsed(bFolded).Count(PerSchool[School]).bDivider(!bFirst)
					.OnToggle_Lambda([this, ThisSchool]()
					{
						if (CollapsedSpellSchools.Remove(ThisSchool) == 0)
						{
							CollapsedSpellSchools.Add(ThisSchool);
						}
					})
			];
			bFirst = false;
		}
		if (bFolded)
		{
			continue;
		}
		FString Reagents;
		for (const FMRReagent& R : Def->Reagents)
		{
			const FMRItemDef* Item = Data->FindItem(R.Item);
			Reagents += FString::Printf(TEXT("%s%d %s"), Reagents.IsEmpty() ? TEXT("") : TEXT(", "), R.Count,
				Item ? *Item->Name.ToString() : *R.Item.ToString());
		}
		const int32 Pct = Src->GetSpellPercent(Known[i]);
		const FText Info = FText::Format(LOCTEXT("SpellInfo", "Level {0}  ·  {1} mana{2}{3}"), FText::AsNumber(Def->Level), FText::AsNumber(Def->Mana),
			Pct >= 0 ? FText::Format(LOCTEXT("SpellPct", "  ·  {0}%"), FText::AsNumber(Pct)) : FText::GetEmpty(),
			Reagents.IsEmpty() ? FText::GetEmpty() : FText::FromString(TEXT("  ·  ") + Reagents));
		const FMRSlotRef Ref(EMRSlotArea::SpellBook, i);
		TSharedRef<SMRListRow> Row = SNew(SMRListRow, Ui, Ref)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRSlot, Ui, Ref).Size(SlotPx).bToolTip(false)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f * Px, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Def->Name, 10.f, true)]
				+ SVerticalBox::Slot().AutoHeight()[Label(S, Info, 7.5f, false, FLinearColor(0.78f, 0.76f, 0.7f))]
			]
		];
		// one tooltip for the whole row (the slot's own is off)
		Row->SetToolTip(Ui->MakeToolTip(FMRSlotContent::Spell(Known[i])));
		SpellList->AddSlot().AutoHeight().Padding(2.f * Px, 0.5f * Px)
		[
			Row
		];
	}
	if (Order.Num() == 0)
	{
		SpellList->AddSlot().AutoHeight().Padding(4.f * Px)
		[
			Label(S, Filter.IsEmpty() ? LOCTEXT("NoSpells", "You know no spells yet.") : LOCTEXT("NoSpellMatch", "No spell matches."), 9.f, false,
				FLinearColor(0.75f, 0.73f, 0.68f))
		];
	}
	TSharedRef<STextBlock> Help = Label(S, LOCTEXT("SpellHelp", "Click a spell and put it on the spell bar, or shift-click it. Numpad 1-9 casts."), 7.5f,
		false, FLinearColor(0.7f, 0.68f, 0.62f));
	Help->SetAutoWrapText(true);
	SpellList->AddSlot().AutoHeight().Padding(2.f * Px, 6.f * Px, 2.f * Px, 2.f * Px)
	[
		Help
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
	const FString Filter = SkillSearch.IsValid() ? SkillSearch->GetText().TrimStartAndEnd() : FString();
	FString Key = Filter + TEXT("|");
	for (const TPair<FName, int32>& P : Skills)
	{
		Key += FString::Printf(TEXT("%s:%d,"), *P.Key.ToString(), P.Value);
	}
	for (int32 School : CollapsedSkillSchools)
	{
		Key += FString::Printf(TEXT("c%d"), School);
	}
	if (Key == SkillsKey)
	{
		return;
	}
	SkillsKey = Key;
	SkillList->ClearChildren();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const float IconPx = S->Number(TEXT("list_slot_px"), 18.f);

	// by school (crafting, weaponcraft, brawling...), then name
	TArray<FName> Names;
	for (const TPair<FName, int32>& P : Skills)
	{
		const FMRSkillDef* Def = Data->FindSkill(P.Key);
		if (MatchesFilter(Filter, Def ? Def->Name.ToString() : P.Key.ToString(), Def ? UMRGameDataSubsystem::SchoolName(Def->School).ToString() : FString()))
		{
			Names.Add(P.Key);
		}
	}
	auto SchoolOf = [Data](const FName& N) { const FMRSkillDef* D = Data->FindSkill(N); return D ? D->School : 0; };
	auto NameOf = [Data](const FName& N) { const FMRSkillDef* D = Data->FindSkill(N); return D ? D->Name.ToString() : N.ToString(); };
	Names.Sort([&](const FName& A, const FName& B)
	{
		if (SchoolOf(A) != SchoolOf(B)) return SchoolOf(A) < SchoolOf(B);
		return NameOf(A) < NameOf(B);
	});
	TMap<int32, int32> PerSchool;
	for (const FName& N : Names)
	{
		PerSchool.FindOrAdd(SchoolOf(N))++;
	}
	int32 School = -1;
	bool bFirst = true;
	for (const FName& Name : Names)
	{
		const FMRSkillDef* Def = Data->FindSkill(Name);
		const int32 ThisSchool = SchoolOf(Name);
		const bool bFolded = Filter.IsEmpty() && CollapsedSkillSchools.Contains(ThisSchool);
		if (ThisSchool != School)
		{
			School = ThisSchool;
			SkillList->AddSlot().AutoHeight().Padding(0.f, bFirst ? 0.f : 2.f * Px, 0.f, 1.f * Px)
			[
				SNew(SMRSectionHeader, Ui).Title(UMRGameDataSubsystem::SchoolName(School)).bCollapsed(bFolded).Count(PerSchool[School]).bDivider(!bFirst)
					.OnToggle_Lambda([this, ThisSchool]()
					{
						if (CollapsedSkillSchools.Remove(ThisSchool) == 0)
						{
							CollapsedSkillSchools.Add(ThisSchool);
						}
					})
			];
			bFirst = false;
		}
		if (bFolded)
		{
			continue;
		}
		const int32 Pct = Skills[Name];
		const FSlateBrush* Icon = Def ? S->Icon(Def->Icon) : nullptr;
		TSharedRef<SMRListRow> Row = SNew(SMRListRow, Ui, FMRSlotRef())
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
				SNew(SMRBar, Ui).Width(50.f).Height(5.f).bShowText(false).Value(static_cast<float>(Pct)).Max(100.f)
					.Color(S->Color(TEXT("skill"), FLinearColor(0.85f, 0.6f, 0.05f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(26.f * Px).HAlign(HAlign_Right)
				[
					Label(S, FText::Format(LOCTEXT("Pct", "{0}%"), FText::AsNumber(Pct)), 10.f, true)
				]
			]
		];
		Row->SetToolTip(Ui->MakeSkillToolTip(Name, Pct));
		SkillList->AddSlot().AutoHeight().Padding(2.f * Px, 1.f * Px)
		[
			Row
		];
	}
	if (Names.Num() == 0)
	{
		SkillList->AddSlot().AutoHeight().Padding(4.f * Px)
		[
			Label(S, Filter.IsEmpty() ? LOCTEXT("NoSkills", "You have no skills yet.") : LOCTEXT("NoSkillMatch", "No skill matches."), 9.f, false,
				FLinearColor(0.75f, 0.73f, 0.68f))
		];
	}
}

// ------------------------------------------------------------------------------ stats

TSharedRef<SWidget> SMRInventoryScreen::MakeStatsPage()
{
	UMRUIStyle* S = UI->GetStyle();
	const float Px = S->Px();
	const float SlotPx = S->Number(TEXT("slot_px"), 22.f);
	StatList = SNew(SVerticalBox);
	return SNew(SMRPanel, UI.Get()).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
	[
		SNew(SBox).MaxDesiredHeight(SlotPx * 2.f * Px).MaxDesiredWidth(SlotPx * 2.f * Px)
		[
			SNew(SScrollBox).ScrollBarThickness(FVector2D(6.f * Px, 6.f * Px))
			+ SScrollBox::Slot()[StatList.ToSharedRef()]
		]
	];
}

void SMRInventoryScreen::RebuildStats(bool bForce)
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui || !StatList.IsValid())
	{
		return;
	}
	if (!bForce && Ui->GetStatsVersion() == StatsShown)
	{
		return;
	}
	StatsShown = Ui->GetStatsVersion();
	StatList->ClearChildren();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const FLinearColor Dim(0.78f, 0.76f, 0.7f);
	const FLinearColor Gold = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));

	// the server's stats (its list, its names) in the ruleset's sections (data/ui/stat_layout.json)
	TArray<FMRStatSection> Sections;
	Ui->GetStatSections(Sections);
	auto ValueText = [](const FMRStatView& V)
	{
		return V.Text.IsEmpty() ? FText::AsNumber(V.Value) : FText::FromString(V.Text);
	};
	bool bFirst = true;
	for (const FMRStatSection& Sec : Sections)
	{
		const FString Title = Sec.Title.ToString();
		if (Sec.bPoints)
		{
			// spendable points: apart, in their own sunk box at the top, the numbers in gold
			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f * Px)[Label(S, Sec.Title, 9.f, true, Gold)];
			for (const FMRStatView& V : Sec.Stats)
			{
				Box->AddSlot().AutoHeight().Padding(4.f * Px, 0.5f * Px)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(S, FText::FromString(V.Name), 10.f)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Label(S, ValueText(V), 12.f, true, Gold)]
				];
			}
			StatList->AddSlot().AutoHeight().Padding(1.f * Px, 1.f * Px, 1.f * Px, 5.f * Px)
			[
				SNew(SMRPanel, Ui).Background(TEXT("bkgnd")).Frame(TEXT("inset")).Padding(3.f)
				[
					Box
				]
			];
			continue;
		}
		const bool bFolded = CollapsedStatSections.Contains(Title);
		StatList->AddSlot().AutoHeight().Padding(0.f, bFirst ? 0.f : 2.f * Px, 0.f, 1.f * Px)
		[
			SNew(SMRSectionHeader, Ui).Title(Sec.Title).bCollapsed(bFolded).Count(Sec.Stats.Num()).bDivider(!bFirst)
				.OnToggle_Lambda([this, Title]()
				{
					if (CollapsedStatSections.Remove(Title) == 0)
					{
						CollapsedStatSections.Add(Title);
					}
					bStatsDirty = true;  // rebuilt next tick, not inside this header's own click
				})
		];
		bFirst = false;
		if (bFolded)
		{
			continue;
		}
		for (const FMRStatView& V : Sec.Stats)
		{
			TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(S, FText::FromString(V.Name), 9.5f)];
			if (V.Max > 0 && V.Text.IsEmpty())
			{
				Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f * Px, 0.f)
				[
					SNew(SMRBar, Ui).Width(60.f).Height(5.f).bShowText(false).Value(static_cast<float>(FMath::Max(0, V.Value)))
						.Max(static_cast<float>(V.Max)).Color(S->Color(TEXT("stat"), FLinearColor(0.6f, 0.5f, 0.35f)))
				];
			}
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(30.f * Px).HAlign(HAlign_Right)[Label(S, ValueText(V), 9.5f, true)]
			];
			StatList->AddSlot().AutoHeight().Padding(6.f * Px, 0.5f * Px, 3.f * Px, 0.5f * Px)
			[
				Line
			];
		}
	}
	if (Sections.Num() == 0)
	{
		StatList->AddSlot().AutoHeight().Padding(4.f * Px)
		[
			Label(S, LOCTEXT("NoStats", "Waiting for the server..."), 9.f, false, Dim)
		];
	}
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

void SMRInventoryScreen::DebugSearch(EMRInventoryTab Page, const FString& Text)
{
	TSharedPtr<SMRTextField>& Search = Page == EMRInventoryTab::Skills ? SkillSearch : SpellSearch;
	if (Search.IsValid())
	{
		Search->SetText(Text);
	}
}

void SMRInventoryScreen::DebugFoldSpellSchool(int32 School)
{
	if (CollapsedSpellSchools.Remove(School) == 0)
	{
		CollapsedSpellSchools.Add(School);
	}
}

void SMRInventoryScreen::OnOpened()
{
	RebuildBag();
	RebuildSpells();
	RebuildSkills();
	RebuildStats(true);  // offline, the attributes may have changed since
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMRInventoryScreen::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geo, Time, Dt);
	RebuildBag();  // grows a row when the last one fills
	// the lists follow the search, the folds and the server's updates (each rebuilds only on a change)
	RebuildSpells();
	RebuildSkills();
	RebuildStats(bStatsDirty);
	bStatsDirty = false;
	// typing in a search bar: every key goes to it (no walking, no hotbar keys)
	if (UMRUISubsystem* Ui = UI.Get())
	{
		Ui->SetTextInput(IsTyping());
	}
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
	if (IsTyping())
	{
		return FReply::Unhandled();  // letters and numbers belong to the search bar
	}
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

bool SMRInventoryScreen::IsTyping() const
{
	return (SpellSearch.IsValid() && SpellSearch->HasFocus()) || (SkillSearch.IsValid() && SkillSearch->HasFocus());
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
