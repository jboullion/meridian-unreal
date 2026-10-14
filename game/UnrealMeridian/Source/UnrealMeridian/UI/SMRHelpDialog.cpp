#include "UI/SMRHelpDialog.h"

#include "Core/MRSettings.h"
#include "Rendering/DrawElements.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRHelp"

namespace
{
	constexpr float WidthPx = 430.f;  // the window, in original pixels
	constexpr float KeyUnit = 21.f;   // a key cap's width, in original pixels
	constexpr float KeyGap = 1.5f;

	/** Shards' HelpDialog.tsx GROUPS: what a key does, and its colour. */
	struct FGroup
	{
		FText Label;
		FLinearColor Color;
	};
	const TArray<FGroup>& Groups()
	{
		static const TArray<FGroup> List = {
			{LOCTEXT("GMove", "Movement"), FLinearColor(FColor(0x4f, 0x9f, 0xe8))},
			{LOCTEXT("GInteract", "Interaction"), FLinearColor(FColor(0xe0, 0xa0, 0x30))},
			{LOCTEXT("GTarget", "Targeting"), FLinearColor(FColor(0xe0, 0x50, 0x50))},
			{LOCTEXT("GTalk", "Communication"), FLinearColor(FColor(0x5e, 0xc4, 0x6e))},
			{LOCTEXT("GInterface", "Interface and quick slots"), FLinearColor(FColor(0xb0, 0x7a, 0xe0))},
			{LOCTEXT("GMap", "Map"), FLinearColor(FColor(0x40, 0xc8, 0xc0))},
			{LOCTEXT("GAlias", "Quick chat (online)"), FLinearColor(FColor(0xa0, 0xa0, 0xa0))},
		};
		return List;
	}
	constexpr int32 GroupInterface = 4, GroupAlias = 6;

	struct FUse
	{
		FText Label;
		int32 Group = INDEX_NONE;
	};

	/** What every key does now: the bindings (MRKeys) and the fixed keys (AMRPlayerController::MapUIKeys). */
	TMap<FKey, TArray<FUse>> KeyUses()
	{
		TMap<FKey, TArray<FUse>> Uses;
		auto Add = [&Uses](const FKey& Key, const FText& Label, int32 Group)
		{
			if (Key.IsValid())
			{
				Uses.FindOrAdd(Key).Add({Label, Group});
			}
		};
		for (const FMRKeyBinding& B : MRKeys::All())
		{
			Add(MRKeys::Get(B.Id), B.Label, SMRHelpDialog::GroupOf(B.Id));
		}
		// the inventory's second key, the menu, the quick slots and the quick chat
		Add(MRKeys::Get(TEXT("Inventory")) == EKeys::I ? EKeys::E : EKeys::I, LOCTEXT("Inventory2", "Inventory"), GroupInterface);
		Add(EKeys::Escape, LOCTEXT("Menu", "The menu (clears the target first)"), GroupInterface);
		Add(EKeys::F10, LOCTEXT("Menu2", "The menu"), GroupInterface);
		const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Digits); ++i)
		{
			Add(Digits[i], FText::Format(LOCTEXT("QuickSlot", "Quick slot {0}"), FText::AsNumber(i + 1)), GroupInterface);
		}
		const FKey FKeys[] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::Invalid,
			EKeys::F11, EKeys::F12};
		for (int32 i = 0; i < UE_ARRAY_COUNT(FKeys); ++i)
		{
			Add(FKeys[i], FText::Format(LOCTEXT("QuickChat", "Quick chat {0} (Options > Chat)"), FText::AsNumber(i + 1)), GroupAlias);
		}
		Add(EKeys::F1, LOCTEXT("HelpKey", "Help (offline)"), GroupInterface);
		return Uses;
	}

	/** A key on the drawing: its cap's label, its key, its width in key units (0: a gap). */
	struct FCap
	{
		const TCHAR* Label;
		FKey Key;
		float Units = 1.f;
	};
}

/** One key cap on the Controls page: coloured by its first use, named on hover. */
class SMRHelpKey : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRHelpKey) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI, SMRHelpDialog* InDialog, const FCap& InCap, const TArray<FUse>* InUses)
	{
		UI = InUI;
		Dialog = InDialog;
		Cap = InCap;
		if (InUses)
		{
			Uses = *InUses;
		}
		TArray<FString> Lines;
		for (const FUse& U : Uses)
		{
			Lines.Add(U.Label.ToString());
		}
		if (Lines.Num() > 0)
		{
			SetToolTipText(FText::FromString(FString::Printf(TEXT("%s: %s"), Cap.Label, *FString::Join(Lines, TEXT("; ")))));
		}
	}
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const float Px = UI.IsValid() && UI->GetStyle() ? UI->GetStyle()->Px() : 2.f;
		return FVector2D(Cap.Units * KeyUnit - KeyGap, KeyUnit - KeyGap) * Px;
	}
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
	{
		UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
		if (!S || !Cap.Key.IsValid())
		{
			return Layer;
		}
		const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
		const FVector2f Size(Geo.GetLocalSize());
		const float Px = S->Px();
		const int32 Group = Uses.Num() > 0 ? Uses[0].Group : INDEX_NONE;
		bool bLit = Dialog && Dialog->FocusGroup != INDEX_NONE && Uses.ContainsByPredicate([this](const FUse& U) { return U.Group == Dialog->FocusGroup; });
		const bool bDim = Dialog && Dialog->FocusGroup != INDEX_NONE && !bLit;
		FLinearColor Fill = Groups().IsValidIndex(Group) ? Groups()[Group].Color : FLinearColor(0.05f, 0.05f, 0.05f);
		if (bLit && Groups().IsValidIndex(Dialog->FocusGroup))
		{
			Fill = Groups()[Dialog->FocusGroup].Color;
		}
		Fill.A = bDim ? 0.25f : (Group == INDEX_NONE ? 0.6f : 0.85f);
		MRPaint::Rounded(Out, Layer, Geo, FVector2f::ZeroVector, Size, Fill * Tint, 2.f * Px,
			(IsHovered() || bLit ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(0.f, 0.f, 0.f, 0.8f)) * Tint, 0.5f * Px);
		const FSlateFontInfo Font = S->Font(6.f, true);
		const FString Label = Cap.Label;
		const FVector2f M = MRPaint::MeasureText(Label, Font);
		MRPaint::Text(Out, Layer + 1, Geo, Label, Font, (Size - M) * 0.5f, FLinearColor(1.f, 1.f, 1.f, bDim ? 0.4f : 1.f) * Tint, 0.4f * Px);
		return Layer + 2;
	}

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	SMRHelpDialog* Dialog = nullptr;
	FCap Cap;
	TArray<FUse> Uses;
};

/** A line pointing at something: a group's swatch (Controls) or a HUD part's number (HUD); hovering it lights its keys or outline. */
class SMRHelpLine : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRHelpLine) : _Group(INDEX_NONE), _Part(INDEX_NONE) {}
		SLATE_ARGUMENT(int32, Group)
		SLATE_ARGUMENT(int32, Part)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SMRHelpDialog* InDialog)
	{
		Dialog = InDialog;
		Group = InArgs._Group;
		Part = InArgs._Part;
		ChildSlot[InArgs._Content.Widget];
	}
	virtual void OnMouseEnter(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		SCompoundWidget::OnMouseEnter(Geo, Event);
		if (Dialog)
		{
			if (Group != INDEX_NONE)
			{
				Dialog->FocusGroup = Group;
			}
			if (Part != INDEX_NONE)
			{
				Dialog->HotPart = Part;
			}
		}
	}
	virtual void OnMouseLeave(const FPointerEvent& Event) override
	{
		SCompoundWidget::OnMouseLeave(Event);
		if (Dialog)
		{
			if (Group != INDEX_NONE && Dialog->FocusGroup == Group)
			{
				Dialog->FocusGroup = INDEX_NONE;
			}
			if (Part != INDEX_NONE && Dialog->HotPart == Part)
			{
				Dialog->HotPart = INDEX_NONE;
			}
		}
	}

private:
	SMRHelpDialog* Dialog = nullptr;
	int32 Group = INDEX_NONE;
	int32 Part = INDEX_NONE;
};

// ------------------------------------------------------------------------------ the dialog

int32 SMRHelpDialog::GroupOf(FName B)
{
	static const TMap<FName, int32> Map = {
		{TEXT("MoveForward"), 0}, {TEXT("MoveBack"), 0}, {TEXT("StrafeLeft"), 0}, {TEXT("StrafeRight"), 0}, {TEXT("TurnLeft"), 0},
		{TEXT("TurnRight"), 0}, {TEXT("Walk"), 0}, {TEXT("Go"), 0},
		{TEXT("Look"), 1}, {TEXT("Get"), 1}, {TEXT("Use"), 1}, {TEXT("Rest"), 1}, {TEXT("Apply"), 1},
		{TEXT("Attack"), 2}, {TEXT("TargetNext"), 2}, {TEXT("TargetPrevious"), 2}, {TEXT("TargetSelf"), 2}, {TEXT("TargetAim"), 2},
		{TEXT("Chat"), 3}, {TEXT("Who"), 3}, {TEXT("Mail"), 3}, {TEXT("Guild"), 3}, {TEXT("QuickEmotes"), 3},
		{TEXT("Inventory"), 4}, {TEXT("View"), 4}, {TEXT("HideInterface"), 4},
		{TEXT("Map"), 5}, {TEXT("MapZoomIn"), 5}, {TEXT("MapZoomOut"), 5},
	};
	const int32* G = Map.Find(B);
	return G ? *G : INDEX_NONE;
}

void SMRHelpDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Help);
	Rebuild();
}

void SMRHelpDialog::SetPage(const FString& InPage)
{
	Page = InPage;
	FocusGroup = HotPart = INDEX_NONE;
	Rebuild();
}

void SMRHelpDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMRHelpDialog> Weak = SharedThis(this);
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (const TCHAR* T : {TEXT("Controls"), TEXT("HUD"), TEXT("Interface")})
	{
		const FString Name = T;
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(Name)).TextSize(9.f).MinWidth(50.f).bActive(Page == Name)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak, Name]() { if (TSharedPtr<SMRHelpDialog> D = Weak.Pin()) D->SetPage(Name); }))
		];
	}
	const TSharedRef<SWidget> Content = Page == TEXT("HUD") ? MakeHud() : Page == TEXT("Interface") ? MakeInterface() : MakeControls();
	SetFrame(LOCTEXT("Title", "Help"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f * Px)[Tabs]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).MaxDesiredHeight(330.f * Px)[SNew(SScrollBox) + SScrollBox::Slot()[Content]]]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 4.f * Px, 0.f, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Close", "Close")).TextSize(9.f).MinWidth(44.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMRHelpDialog> D = Weak.Pin()) D->Close(); }))
		],
		WidthPx);
}

TSharedRef<SWidget> SMRHelpDialog::MakeControls()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const TMap<FKey, TArray<FUse>> Uses = KeyUses();
	const float Half = 0.5f;
	const TArray<TArray<FCap>> Rows = {
		{{TEXT("Esc"), EKeys::Escape}, {TEXT(""), FKey(), Half}, {TEXT("F1"), EKeys::F1}, {TEXT("F2"), EKeys::F2}, {TEXT("F3"), EKeys::F3}, {TEXT("F4"), EKeys::F4},
			{TEXT(""), FKey(), Half}, {TEXT("F5"), EKeys::F5}, {TEXT("F6"), EKeys::F6}, {TEXT("F7"), EKeys::F7}, {TEXT("F8"), EKeys::F8},
			{TEXT(""), FKey(), Half}, {TEXT("F9"), EKeys::F9}, {TEXT("F10"), EKeys::F10}, {TEXT("F11"), EKeys::F11}, {TEXT("F12"), EKeys::F12}},
		{{TEXT("`"), EKeys::Tilde}, {TEXT("1"), EKeys::One}, {TEXT("2"), EKeys::Two}, {TEXT("3"), EKeys::Three}, {TEXT("4"), EKeys::Four},
			{TEXT("5"), EKeys::Five}, {TEXT("6"), EKeys::Six}, {TEXT("7"), EKeys::Seven}, {TEXT("8"), EKeys::Eight}, {TEXT("9"), EKeys::Nine},
			{TEXT("0"), EKeys::Zero}, {TEXT("-"), EKeys::Hyphen}, {TEXT("="), EKeys::Equals}, {TEXT("Back"), EKeys::BackSpace, 2.f},
			{TEXT(""), FKey(), Half}, {TEXT("Home"), EKeys::Home}, {TEXT("End"), EKeys::End}},
		{{TEXT("Tab"), EKeys::Tab, 1.5f}, {TEXT("Q"), EKeys::Q}, {TEXT("W"), EKeys::W}, {TEXT("E"), EKeys::E}, {TEXT("R"), EKeys::R},
			{TEXT("T"), EKeys::T}, {TEXT("Y"), EKeys::Y}, {TEXT("U"), EKeys::U}, {TEXT("I"), EKeys::I}, {TEXT("O"), EKeys::O}, {TEXT("P"), EKeys::P},
			{TEXT("["), EKeys::LeftBracket}, {TEXT("]"), EKeys::RightBracket}, {TEXT("\\"), EKeys::Backslash, 1.5f}},
		{{TEXT("Caps"), EKeys::CapsLock, 1.75f}, {TEXT("A"), EKeys::A}, {TEXT("S"), EKeys::S}, {TEXT("D"), EKeys::D}, {TEXT("F"), EKeys::F},
			{TEXT("G"), EKeys::G}, {TEXT("H"), EKeys::H}, {TEXT("J"), EKeys::J}, {TEXT("K"), EKeys::K}, {TEXT("L"), EKeys::L},
			{TEXT(";"), EKeys::Semicolon}, {TEXT("'"), EKeys::Apostrophe}, {TEXT("Enter"), EKeys::Enter, 2.25f}},
		{{TEXT("Shift"), EKeys::LeftShift, 2.25f}, {TEXT("Z"), EKeys::Z}, {TEXT("X"), EKeys::X}, {TEXT("C"), EKeys::C}, {TEXT("V"), EKeys::V},
			{TEXT("B"), EKeys::B}, {TEXT("N"), EKeys::N}, {TEXT("M"), EKeys::M}, {TEXT(","), EKeys::Comma}, {TEXT("."), EKeys::Period},
			{TEXT("/"), EKeys::Slash}, {TEXT("Shift"), EKeys::RightShift, 2.75f}, {TEXT(""), FKey(), 1.5f}, {TEXT("Up"), EKeys::Up}},
		{{TEXT("Ctrl"), EKeys::LeftControl, 1.5f}, {TEXT(""), FKey(), 1.25f}, {TEXT("Alt"), EKeys::LeftAlt, 1.25f}, {TEXT("Space"), EKeys::SpaceBar, 6.f},
			{TEXT("Alt"), EKeys::RightAlt, 1.25f}, {TEXT(""), FKey(), 1.25f}, {TEXT("Ctrl"), EKeys::RightControl, 1.5f}, {TEXT(""), FKey(), 0.5f},
			{TEXT("Left"), EKeys::Left}, {TEXT("Down"), EKeys::Down}, {TEXT("Right"), EKeys::Right}},
	};
	TSharedRef<SVerticalBox> Board = SNew(SVerticalBox);
	for (const TArray<FCap>& Row : Rows)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (const FCap& Cap : Row)
		{
			if (!Cap.Key.IsValid())
			{
				Line->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(Cap.Units * KeyUnit * Px)];
				continue;
			}
			Line->AddSlot().AutoWidth().Padding(0.f, 0.f, KeyGap * Px, 0.f)
			[
				SNew(SMRHelpKey, Ui, this, Cap, Uses.Find(Cap.Key))
			];
		}
		Board->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, KeyGap * Px)[Line];
	}

	// the groups, each lighting its keys when pointed at
	TSharedRef<SWrapBox> Legend = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 2.f) * Px);
	for (int32 i = 0; i < Groups().Num(); ++i)
	{
		Legend->AddSlot()
		[
			SNew(SMRHelpLine, this).Group(i)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f * Px, 0.f)
				[
					SNew(SBox).WidthOverride(7.f * Px).HeightOverride(7.f * Px)
					[
						SNew(SImage).Image(S->White()).ColorAndOpacity(Groups()[i].Color)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MRUI::Label(S, Groups()[i].Label, 8.f)
				]
			]
		];
	}

	// the mouse: its buttons as bound now, and the wheel
	TSharedRef<SVerticalBox> Mouse = SNew(SVerticalBox);
	for (const TPair<FKey, TArray<FUse>>& P : Uses)
	{
		if (!P.Key.IsMouseButton())
		{
			continue;
		}
		TArray<FString> Lines;
		for (const FUse& U : P.Value)
		{
			Lines.Add(U.Label.ToString());
		}
		Mouse->AddSlot().AutoHeight()
		[
			MRUI::Label(S, FText::Format(LOCTEXT("MouseLine", "{0}: {1}"), P.Key.GetDisplayName(), FText::FromString(FString::Join(Lines, TEXT("; ")))), 8.f)
		];
	}
	Mouse->AddSlot().AutoHeight()
	[
		MRUI::Label(S, LOCTEXT("MouseRight", "Right button held: turn the view (the cursor stays free otherwise: Options > Controls)."), 8.f)
	];
	Mouse->AddSlot().AutoHeight()
	[
		MRUI::Label(S, LOCTEXT("MouseWheel", "Wheel: zoom the camera. In the inventory: click picks up and places, shift-click wears or takes off, a number names it on a quick slot."), 8.f)
	];
	Mouse->AddSlot().AutoHeight()
	[
		MRUI::Label(S, LOCTEXT("Numpad", "Numpad 1-9 and 0: the second row of quick slots. The keys can be changed in Options > Controls."), 8.f)
	];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			MRUI::Label(S, LOCTEXT("Keyboard", "Keyboard"), 10.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Board]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Legend]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f * Px, 0.f, 3.f * Px)
		[
			MRUI::Label(S, LOCTEXT("MouseTitle", "Mouse"), 10.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
		]
		+ SVerticalBox::Slot().AutoHeight()[Mouse];
}

TSharedRef<SWidget> SMRHelpDialog::MakeHud()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TArray<SMRHUDRoot::FHudPart> Parts;
	if (const TSharedPtr<SMRHUDRoot> Root = Ui->GetHUDRoot())
	{
		Root->GetHudParts(Parts);
	}
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f * Px)
		[
			MRUI::Label(S, LOCTEXT("HudNote", "Your screen as it is now: each part of the HUD is outlined and numbered. Point at a line to find its part."), 8.5f)
		];
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		TSharedRef<STextBlock> Text = MRUI::Label(S, FText::Format(LOCTEXT("PartLine", "{0}. {1}"), Parts[i].Title, Parts[i].Text), 8.5f);
		Text->SetAutoWrapText(true);
		List->AddSlot().AutoHeight().Padding(0.f, 1.f * Px)
		[
			SNew(SMRHelpLine, this).Part(i)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 4.f * Px, 0.f)
				[
					MRUI::Label(S, FText::AsNumber(i + 1), 9.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Text
				]
			]
		];
	}
	return List;
}

TSharedRef<SWidget> SMRHelpDialog::MakeInterface()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const FLinearColor Heading = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Section = [&](const FText& Title)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 5.f * Px, 0.f, 2.f * Px)[MRUI::Label(S, Title, 10.f, true, Heading)];
	};
	auto Def = [&](const FText& Term, const FText& Text)
	{
		TSharedRef<STextBlock> T = MRUI::Label(S, Text, 8.5f);
		T->SetAutoWrapText(true);
		Box->AddSlot().AutoHeight().Padding(0.f, 1.f * Px)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MRUI::Label(S, Term, 8.5f, true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(6.f * Px, 0.f, 0.f, 0.f)[T]
		];
	};
	Section(LOCTEXT("MenuTitle", "The menu (Esc or F10)"));
	Def(LOCTEXT("WhoT", "Who Is On (O)"), LOCTEXT("WhoD", "Who is logged on: send a tell, ignore someone, turn broadcasts off."));
	Def(LOCTEXT("MailT", "Mail (L)"), LOCTEXT("MailD", "Your letters: read, reply, write, get new mail."));
	Def(LOCTEXT("GuildT", "Guild (Y)"), LOCTEXT("GuildD", "Your guild's members and ranks, its allies and enemies, or founding one."));
	Def(LOCTEXT("HelpT", "Help"), LOCTEXT("HelpD", "This window."));
	Def(LOCTEXT("OptionsT", "Options"), LOCTEXT("OptionsD", "Graphics, sound, your keys (Modern, or the original's), the game's options and the HUD's size, the quick chat (F1-F12) and your account."));
	Def(LOCTEXT("AdminT", "Admin Console"), LOCTEXT("AdminD", "For admins and DMs: travel, players, and commands on the live world."));
	Def(LOCTEXT("LogOffT", "Log Off"), LOCTEXT("LogOffD", "Back to your characters."));
	Section(LOCTEXT("CharTitle", "Your character (E or I)"));
	Def(LOCTEXT("InvT", "Inventory"), LOCTEXT("InvD", "What you wear and carry. Click picks up and places; shift-click wears or takes off; drop it on yourself to put it on, outside the window to drop it. Point at an item and press 1-0 to put it on that quick slot."));
	Def(LOCTEXT("SpellsT", "Spells and Skills"), LOCTEXT("SpellsD", "What you know and how well. Search by name; pick a spell up and put it on a quick slot, or shift-click it."));
	Def(LOCTEXT("StatsT", "Stats and Quests"), LOCTEXT("StatsD", "Your stats, karma, what you carry, your experience; the quests you're on."));
	Section(LOCTEXT("OtherTitle", "Other windows"));
	Def(LOCTEXT("LookT", "Look"), LOCTEXT("LookD", "Right click anything: what it is, with buttons to get it, look inside or use it."));
	Def(LOCTEXT("ShopT", "Shops, banks and vaults"), LOCTEXT("ShopD", "Use (F) a shopkeeper, banker or vault keeper: buy and sell, deposit and withdraw."));
	Def(LOCTEXT("TradeT", "Trading"), LOCTEXT("TradeD", "Another player's offer opens a window to look it over and take it or turn it down."));
	Def(LOCTEXT("MapT", "The large map (M)"), LOCTEXT("MapD", "The whole room, with your notes on it: click to mark a spot, then write."));
	Def(LOCTEXT("NewsT", "News boards"), LOCTEXT("NewsD", "Look at a board to read and post."));
	return Box;
}

int32 SMRHelpDialog::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	int32 Top = SMRSocialWindow::OnPaint(Args, Geo, Culling, Out, Layer, WStyle, bParentEnabled);
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	const TSharedPtr<SMRHUDRoot> Root = Ui ? Ui->GetHUDRoot() : nullptr;
	if (Page != TEXT("HUD") || !S || !Root.IsValid())
	{
		return Top;
	}
	// the HUD page: each part outlined and numbered where it stands (Shards' .hud-box and .hud-num)
	TArray<SMRHUDRoot::FHudPart> Parts;
	Root->GetHudParts(Parts);
	const float Px = S->Px();
	const FLinearColor Gold(1.f, 0.75f, 0.2f);
	const FSlateFontInfo Font = S->Font(8.f, true);
	const float Badge = 10.f * Px;
	TArray<FVector2f> Placed;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		const TSharedPtr<SWidget> W = Parts[i].Widget.Pin();
		if (!W.IsValid())
		{
			continue;
		}
		const FGeometry& G = W->GetPaintSpaceGeometry();
		const FVector2f At = FVector2f(Geo.AbsoluteToLocal(G.GetAbsolutePosition()));
		const FVector2f Size = FVector2f(G.GetAbsoluteSize()) / FMath::Max(0.01f, Geo.Scale);
		const bool bHot = i == HotPart;
		MRPaint::Rounded(Out, Top + 1, Geo, At - FVector2f(Px, Px), Size + FVector2f(2.f * Px, 2.f * Px),
			Gold * FLinearColor(1.f, 1.f, 1.f, bHot ? 0.18f : 0.f), 2.f * Px, Gold * FLinearColor(1.f, 1.f, 1.f, bHot ? 1.f : 0.7f), (bHot ? 1.f : 0.5f) * Px);
		// the number at its top left, moved right (then down) off any placed before
		FVector2f B = At - FVector2f(Badge * 0.4f, Badge * 0.4f);
		for (int32 Tries = 0; Tries < 12 && Placed.ContainsByPredicate([&](const FVector2f& P) { return FMath::Abs(P.X - B.X) < Badge && FMath::Abs(P.Y - B.Y) < Badge; }); ++Tries)
		{
			B.X += Badge;
			if (B.X > At.X + Size.X)
			{
				B.X = At.X;
				B.Y += Badge;
			}
		}
		Placed.Add(B);
		MRPaint::Rounded(Out, Top + 2, Geo, B, FVector2f(Badge, Badge), (bHot ? Gold : FLinearColor(0.1f, 0.07f, 0.03f, 0.95f)), Badge * 0.5f, Gold, 0.5f * Px);
		const FString N = FString::FromInt(i + 1);
		const FVector2f M = MRPaint::MeasureText(N, Font);
		MRPaint::Text(Out, Top + 3, Geo, N, Font, B + (FVector2f(Badge, Badge) - M) * 0.5f, bHot ? FLinearColor::Black : Gold, 0.f);
	}
	return Top + 4;
}

#undef LOCTEXT_NAMESPACE
