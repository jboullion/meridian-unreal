#include "UI/SMRHUD.h"

#include "Abilities/MRAttributeSet.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Rendering/DrawElements.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRChatLog.h"
#include "UI/SMRGameMenu.h"
#include "UI/SMRInventoryScreen.h"
#include "UI/SMRMinimap.h"
#include "UI/SMRSlot.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// ------------------------------------------------------------------------------ MRUI

TSharedRef<STextBlock> MRUI::Label(UMRUIStyle* S, const TAttribute<FText>& Text, float Size, bool bBold, const FLinearColor& Color)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(S->Font(Size, bBold))
		.ColorAndOpacity(Color)
		.ShadowOffset(FVector2D(1.0, 1.0) * S->Px() * 0.5)
		.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
}

// ------------------------------------------------------------------------------ SMRTextButton

void SMRTextButton::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Text = InArgs._Text;
	bActive = InArgs._bActive;
	TextSize = InArgs._TextSize;
	MinWidth = InArgs._MinWidth;
	OnClicked = InArgs._OnClicked;
}

FVector2D SMRTextButton::ComputeDesiredSize(float) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!Style)
	{
		return FVector2D(80.f, 30.f);
	}
	const float H = Style->PieceSize(TEXT("tab_mid_up")).Y;
	const FVector2f T = MRPaint::MeasureText(Text.Get().ToString(), Style->Font(TextSize, true));
	return FVector2D(FMath::Max(MinWidth * Style->Px(), T.X + 16.f * Style->Px()), H > 0.f ? H : T.Y + 8.f * Style->Px());
}

int32 SMRTextButton::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!Style)
	{
		return Layer;
	}
	// the stat button stretched as SMRTab draws it, with a label instead of an icon
	const bool bEnabled = bParentEnabled && IsEnabled();
	const bool bDown = bActive.Get(false);
	const TCHAR* State = bDown ? TEXT("down") : TEXT("up");
	const FVector2f S(Geo.GetLocalSize());
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint() * (bEnabled ? FLinearColor::White : FLinearColor(0.55f, 0.55f, 0.55f, 1.f));
	const FName LeftName(*FString::Printf(TEXT("tab_left_%s"), State)), MidName(*FString::Printf(TEXT("tab_mid_%s"), State));
	const FName RightName(*FString::Printf(TEXT("tab_right_%s"), State));
	const float K = S.Y / FMath::Max(1.f, Style->PieceSize(MidName).Y);
	const FVector2f L = Style->PieceSize(LeftName) * K, R = Style->PieceSize(RightName) * K;
	MRPaint::Box(Out, Layer, Geo, Style->Brush(LeftName), FVector2f::ZeroVector, L, Tint);
	MRPaint::Tile(Out, Layer, Geo, Style->Brush(MidName, true, K), FVector2f(L.X, 0.f), FVector2f(S.X - L.X - R.X, S.Y), Tint);
	MRPaint::Box(Out, Layer, Geo, Style->Brush(RightName), FVector2f(S.X - R.X, 0.f), R, Tint);
	const FString Label = Text.Get().ToString();
	const FSlateFontInfo Font = Style->Font(TextSize, true);
	const FVector2f T = MRPaint::MeasureText(Label, Font);
	const FLinearColor TextColor = bEnabled ? (bDown ? Style->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f))
		: Style->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f))) : FLinearColor(0.6f, 0.58f, 0.52f);
	MRPaint::Text(Out, Layer + 1, Geo, Label, Font, (S - T) * 0.5f + FVector2f(0.f, bDown ? Style->Px() * 0.5f : 0.f), TextColor);
	if (IsHovered() && bEnabled && !bDown)
	{
		MRPaint::Box(Out, Layer + 1, Geo, Style->White(), FVector2f::ZeroVector, S, FLinearColor(1.f, 1.f, 1.f, 0.12f));
	}
	return Layer + 2;
}

FReply SMRTextButton::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && IsEnabled())
	{
		OnClicked.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SMRTextButton::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRTextField

void SMRTextField::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	MaxLength = InArgs._MaxLength;
	OnSubmit = InArgs._OnSubmit;
	OnCancel = InArgs._OnCancel;
	OnChanged = InArgs._OnChanged;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	ChildSlot
	[
		SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
		[
			SNew(SBox).WidthOverride(InArgs._Width * Px).Padding(2.f * Px, 1.f * Px)
			[
				SAssignNew(Edit, SEditableText)
				.Text(InArgs._InitialText)
				.HintText(InArgs._HintText)
				.IsPassword(InArgs._bPassword)
				.Font(S->Font(10.f))
				.ColorAndOpacity(S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)))
				.SelectAllTextWhenFocused(true)
				.ClearKeyboardFocusOnCommit(false)
				.OnTextChanged_Lambda([this](const FText& T)
				{
					if (T.ToString().Len() > MaxLength)
					{
						Edit->SetText(FText::FromString(T.ToString().Left(MaxLength)));
					}
					OnChanged.ExecuteIfBound();
				})
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type How)
				{
					if (How == ETextCommit::OnEnter)
					{
						OnSubmit.ExecuteIfBound();
					}
					else if (How == ETextCommit::OnCleared)
					{
						OnCancel.ExecuteIfBound();  // Escape
					}
				})
			]
		]
	];
}

FString SMRTextField::GetText() const
{
	return Edit.IsValid() ? Edit->GetText().ToString() : FString();
}

void SMRTextField::SetText(const FString& InText)
{
	if (Edit.IsValid())
	{
		Edit->SetText(FText::FromString(InText));
	}
}

void SMRTextField::Focus()
{
	if (Edit.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Edit, EFocusCause::SetDirectly);
	}
}

bool SMRTextField::HasFocus() const
{
	return Edit.IsValid() && Edit->HasKeyboardFocus();
}

// ------------------------------------------------------------------------------ SMRPanel

void SMRPanel::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Background = InArgs._Background;
	Frame = InArgs._Frame;
	bCorners = InArgs._bCorners;
	BackgroundTint = InArgs._BackgroundTint;
	UMRUIStyle* Style = InUI ? InUI->GetStyle() : nullptr;
	FMargin Pad(InArgs._Padding * (Style ? Style->Px() : 2.f));
	if (Style && !Frame.IsNone())
	{
		const FMargin Inset = Style->Frame(Frame).Inset;
		Pad = Pad + Inset;
	}
	if (Style && bCorners)
	{
		Pad = Pad + FMargin(CornerBand(Style));
	}
	ChildSlot.Padding(Pad)[InArgs._Content.Widget];
}

float SMRPanel::CornerBand(UMRUIStyle* Style)
{
	// the 3D view treatment's outer strips (viewtreat_ul_top...) are its frame's thickness
	return Style->Frame(TEXT("view")).Size[FMRFrameBrushes::UL_V].X;
}

int32 SMRPanel::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	const FVector2f Outer(Geo.GetLocalSize());
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	// a cornered panel is drawn like the original 3D view: the window inside a transparent band
	// that the gold-ball corners reach into (drawint.c ELEMENT_ULTOP..., draw3d.c the corners)
	const float Band = Style && bCorners ? CornerBand(Style) : 0.f;
	const FVector2f Pos(Band, Band);
	const FVector2f Size = Outer - 2.f * Pos;
	if (Style)
	{
		if (!Background.IsNone())
		{
			MRPaint::Tile(Out, Layer, Geo, Style->Brush(Background, true), Pos, Size, Tint * BackgroundTint);
		}
		if (!Frame.IsNone())
		{
			// with the gold corners, the frame's own corner strips are left out (one corner image)
			MRPaint::Frame(Out, Layer + 1, Geo, Style->Frame(Frame), Pos, Size, Tint, !bCorners);
		}
	}
	int32 MaxLayer = SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 3, WStyle, bParentEnabled);
	if (Style && bCorners)
	{
		// the gold-ball corners at the window's corners, and their outer strips in the band
		const int32 L = MaxLayer + 1;
		const FVector2f C = Style->PieceSize(TEXT("view_ul")) * Style->FrameScale(TEXT("view"));
		MRPaint::Box(Out, L, Geo, Style->Brush(TEXT("view_ul")), Pos, C, Tint);
		MRPaint::Box(Out, L, Geo, Style->Brush(TEXT("view_ur")), Pos + FVector2f(Size.X - C.X, 0.f), C, Tint);
		MRPaint::Box(Out, L, Geo, Style->Brush(TEXT("view_ll")), Pos + FVector2f(0.f, Size.Y - C.Y), C, Tint);
		MRPaint::Box(Out, L, Geo, Style->Brush(TEXT("view_lr")), Pos + Size - C, C, Tint);
		MRPaint::Frame(Out, L, Geo, Style->Frame(TEXT("view")), FVector2f::ZeroVector, Outer, Tint, true, false);
		MaxLayer = L + 1;
	}
	return MaxLayer;
}

// ------------------------------------------------------------------------------ SMRBar

void SMRBar::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Value = InArgs._Value;
	Max = InArgs._Max;
	Color = InArgs._Color;
	Width = InArgs._Width;
	Height = InArgs._Height;
	bShowText = InArgs._bShowText;
}

FVector2D SMRBar::ComputeDesiredSize(float) const
{
	const float Px = UI.IsValid() && UI->GetStyle() ? UI->GetStyle()->Px() : 2.f;
	return FVector2D(Width * Px, Height * Px);
}

void SMRBar::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	const float M = FMath::Max(1.f, Max.Get());
	const float Target = FMath::Clamp(Value.Get() / M, 0.f, 1.f);
	if (Shown < 0.f)
	{
		Shown = Trail = Target;
	}
	if (Target < Shown - 0.001f)
	{
		FlashTime = 0.35f;  // took a loss
	}
	Shown = FMath::FInterpTo(Shown, Target, Dt, 14.f);
	Trail = Target > Trail ? Target : FMath::FInterpConstantTo(Trail, Target, Dt, 0.35f);
	FlashTime = FMath::Max(0.f, FlashTime - Dt);
}

int32 SMRBar::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!Style)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const FSlateBrush* W = Style->White();
	// the empty bar, the loss trail, the fill (lighter while flashing)
	MRPaint::Box(Out, Layer, Geo, W, FVector2f::ZeroVector, Size, Tint * Style->Color(TEXT("bar_empty"), FLinearColor(0.02f, 0.02f, 0.02f, 0.85f)));
	MRPaint::Box(Out, Layer + 1, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * FMath::Max(0.f, Trail), Size.Y),
		Tint * FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.45f) * FLinearColor(1.f, 1.f, 1.f, 0.7f));
	const FLinearColor Fill = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, FlashTime / 0.35f * 0.5f);
	MRPaint::Box(Out, Layer + 2, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * FMath::Max(0.f, Shown), Size.Y), Tint * Fill);
	// a highlight along the top third, so the fill isn't flat
	MRPaint::Box(Out, Layer + 3, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * FMath::Max(0.f, Shown), Size.Y * 0.3f),
		Tint * FLinearColor(1.f, 1.f, 1.f, 0.18f));

	// the original's gold bar frame: caps scaled to the bar's height, top and bottom repeaters
	const FVector2f CapL = Style->PieceSize(TEXT("bar_left")), CapR = Style->PieceSize(TEXT("bar_right"));
	if (CapL.Y > 0.f && CapR.Y > 0.f)
	{
		const float K = Size.Y / CapL.Y * 1.25f;
		const FVector2f L(CapL.X * K, Size.Y * 1.25f), R(CapR.X * K, Size.Y * 1.25f);
		const float Y = -Size.Y * 0.125f;
		const float Edge = Style->PieceSize(TEXT("bar_top")).Y;
		MRPaint::Tile(Out, Layer + 4, Geo, Style->Brush(TEXT("bar_top"), true), FVector2f(0.f, Y), FVector2f(Size.X, Edge), Tint);
		MRPaint::Tile(Out, Layer + 4, Geo, Style->Brush(TEXT("bar_bottom"), true), FVector2f(0.f, Size.Y - Y - Edge), FVector2f(Size.X, Edge), Tint);
		MRPaint::Box(Out, Layer + 5, Geo, Style->Brush(TEXT("bar_left")), FVector2f(-L.X * 0.6f, Y), L, Tint);
		MRPaint::Box(Out, Layer + 5, Geo, Style->Brush(TEXT("bar_right")), FVector2f(Size.X - R.X * 0.4f, Y), R, Tint);
	}
	if (bShowText)
	{
		const FString Text = FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Value.Get()), FMath::RoundToInt(Max.Get()));
		const FSlateFontInfo Font = Style->Font(FMath::Max(6.f, Height * 0.75f), true);
		const FVector2f M = MRPaint::MeasureText(Text, Font);
		MRPaint::Text(Out, Layer + 6, Geo, Text, Font, (Size - M) * 0.5f, FLinearColor::White * Tint, Style->Px() * 0.5f);
	}
	return Layer + 8;
}

// ------------------------------------------------------------------------------ SMRCursorStack

void SMRCursorStack::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SMRCursorStack::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	if (!Style || !Ui->GetSource() || !Ui->IsInventoryOpen() || !FSlateApplication::IsInitialized())
	{
		return Layer;
	}
	const FMRSlotContent C = Ui->GetSource()->Get(FMRSlotRef(EMRSlotArea::Cursor, 0));
	if (C.IsEmpty())
	{
		return Layer;
	}
	// the position from the UI's own mouse events (the same space hit testing uses): the platform
	// cursor position doesn't match the game viewport's in every setup (an editor PIE window)
	FVector2D Screen;
	if (!Ui->GetMouse(Screen))
	{
		Screen = FSlateApplication::Get().GetCursorPos();
	}
	// events are in tick space; converting with the paint geometry (Geo) is off wherever the two
	// differ (an editor viewport): local coordinates are the same in both
	const FVector2f Mouse = FVector2f(GetTickSpaceGeometry().AbsoluteToLocal(Screen));
	const float S = Style->Px(22.f) * 0.9f;
	const FVector2f Pos = Mouse - FVector2f(S, S) * 0.5f;
	MRPaint::Box(Out, Layer, Geo, Ui->IconFor(C), Pos, FVector2f(S, S));
	if (C.Count > 1)
	{
		const FString Count = FString::FromInt(C.Count);
		const FSlateFontInfo Font = Style->Font(9.f, true);
		const FVector2f M = MRPaint::MeasureText(Count, Font);
		MRPaint::Text(Out, Layer + 1, Geo, Count, Font, Pos + FVector2f(S, S) - M, FLinearColor::White, Style->Px() * 0.5f);
	}
	return Layer + 3;
}

// ------------------------------------------------------------------------------ SMRItemName

void SMRItemName::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(EVisibility::HitTestInvisible);
}

FVector2D SMRItemName::ComputeDesiredSize(float) const
{
	const float Px = UI.IsValid() && UI->GetStyle() ? UI->GetStyle()->Px() : 2.f;
	return FVector2D(200.f * Px, 12.f * Px);
}

int32 SMRItemName::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	if (!Style || !Ui->GetSource())
	{
		return Layer;
	}
	const double Age = Ui->Now() - Ui->LastSelectionTime();
	const float Hold = Style->Number(TEXT("item_name_seconds"), 2.f);
	const float Alpha = FMath::Clamp(1.f - static_cast<float>(Age - Hold) / 0.5f, 0.f, 1.f);
	const FMRSlotContent C = Ui->GetSource()->Get(FMRSlotRef(EMRSlotArea::Hotbar, Ui->GetSource()->GetSelectedHotbar()));
	if (Alpha <= 0.f || C.IsEmpty())
	{
		return Layer;
	}
	const FString Name = Ui->NameFor(C).ToString();
	const FSlateFontInfo Font = Style->Font(11.f, true);
	const FVector2f M = MRPaint::MeasureText(Name, Font);
	const FVector2f Size(Geo.GetLocalSize());
	MRPaint::Text(Out, Layer, Geo, Name, Font, FVector2f((Size.X - M.X) * 0.5f, Size.Y - M.Y),
		Style->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)) * FLinearColor(1.f, 1.f, 1.f, Alpha) * WStyle.GetColorAndOpacityTint(), Style->Px() * 0.6f);
	return Layer + 2;
}

// ------------------------------------------------------------------------------ SMRHUDRoot

void SMRHUDRoot::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SMRHUDRoot::GetHUDVisibility));
	Rebuild();
}

EVisibility SMRHUDRoot::GetHUDVisibility() const
{
	// hidden for photos (AMRCharacter::TakePhoto turns the HUD off for a moment)
	const APlayerController* PC = UI.IsValid() ? UI->GetPlayerController() : nullptr;
	const AHUD* Hud = PC ? PC->GetHUD() : nullptr;
	return Hud && !Hud->bShowHUD ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
}

void SMRHUDRoot::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	if (!Style)
	{
		return;
	}
	const float Px = Style->Px();
	HotbarArea = MakeHotbarArea();
	SpellBar = MakeSpellBar();
	Inventory = SNew(SMRInventoryScreen, Ui);
	Inventory->SetVisibility(Ui->IsInventoryOpen() ? EVisibility::Visible : EVisibility::Collapsed);
	ChatLog = SNew(SMRChatLog, Ui);
	GameMenu = SNew(SMRGameMenu, Ui);
	GameMenu->SetVisibility(Ui->IsGameMenuOpen() ? EVisibility::Visible : EVisibility::Collapsed);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, Style->Number(TEXT("hotbar_bottom"), 6.f) * Px)
		[
			HotbarArea.ToSharedRef()
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, Style->Number(TEXT("minimap_margin"), 8.f) * Px,
			Style->Number(TEXT("minimap_margin"), 8.f) * Px, 0.f)
		[
			SNew(SMRMinimap, Ui)
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(Style->Number(TEXT("chat_margin"), 8.f) * Px)
		[
			ChatLog.ToSharedRef()
		]
		+ SOverlay::Slot()
		[
			Inventory.ToSharedRef()
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, Style->Number(TEXT("spellbar_margin"), 8.f) * Px,
			Style->Number(TEXT("spellbar_margin"), 8.f) * Px)
		[
			SpellBar.ToSharedRef()
		]
		+ SOverlay::Slot()
		[
			SNew(SMRCursorStack, Ui)
		]
		// a room being built from the server's files (UMRRuntimeRooms): say so over the old one
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			MRUI::Label(Style, TAttribute<FText>::CreateLambda([this]() { return FText::FromString(LoadingText()); }), 14.f, true)
		]
		+ SOverlay::Slot()
		[
			GameMenu.ToSharedRef()
		]
	];
}

FString SMRHUDRoot::LoadingText() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	const UMRNetWorldSubsystem* NetWorld = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	return NetWorld && !NetWorld->GetLoadingRoom().IsEmpty() ? FString::Printf(TEXT("Loading %s..."), *NetWorld->GetLoadingRoom()) : FString();
}

TSharedRef<SWidget> SMRHUDRoot::MakeHotbarArea()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui->GetStyle();
	const float Px = Style->Px();
	const float SlotPx = Style->Number(TEXT("slot_px"), 22.f);
	const float RowW = SlotPx * 9.f;
	const float Gap = 4.f;

	TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
	for (int32 i = 0; i < 9; ++i)
	{
		Slots->AddSlot().AutoWidth()
		[
			SNew(SMRSlot, Ui, FMRSlotRef(EMRSlotArea::Hotbar, i)).Size(SlotPx).KeyLabel(FString::FromInt(i + 1)).bSelectable(true)
		];
	}
	// the server's condition stats online, the local attributes offline (UMRUISubsystem::GetVital)
	auto Vital = [Ui](int32 Index, bool bMax)
	{
		return TAttribute<float>::CreateLambda([Ui, Index, bMax]()
		{
			float Value = 0.f, Max = 0.f;
			if (Ui)
			{
				Ui->GetVital(Index, Value, Max);
			}
			return bMax ? Max : Value;
		});
	};
	const float BarW = (RowW - Gap) * 0.5f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 2.f * Px)
		[
			SNew(SMRItemName, Ui)
		]
		// health (left) and mana (right) above the hotbar, where Minecraft has hearts and food
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Gap * Px, 0.f)
			[
				SNew(SMRBar, Ui).Width(BarW).Height(Style->Number(TEXT("bar_px"), 9.f))
					.Color(Style->Color(TEXT("health"), FLinearColor(0.75f, 0.06f, 0.06f)))
					.Value(Vital(0, false)).Max(Vital(0, true))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SMRBar, Ui).Width(BarW).Height(Style->Number(TEXT("bar_px"), 9.f))
					.Color(Style->Color(TEXT("mana"), FLinearColor(0.08f, 0.2f, 0.85f)))
					.Value(Vital(1, false)).Max(Vital(1, true))
			]
		]
		// vigor: a slimmer bar across the hotbar's width, where Minecraft has experience, with its value too
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			SNew(SMRBar, Ui).Width(RowW).Height(Style->Number(TEXT("vigor_bar_px"), 7.f))
				.Color(Style->Color(TEXT("vigor"), FLinearColor(0.85f, 0.6f, 0.05f)))
				.Value(Vital(2, false)).Max(Vital(2, true))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(1.f)
			[
				Slots
			]
		];
}

TSharedRef<SWidget> SMRHUDRoot::MakeSpellBar()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui->GetStyle();
	const float SlotPx = Style->Number(TEXT("spell_slot_px"), 20.f);
	const bool bRow = Style->Number(TEXT("spellbar_row"), 0.f) > 0.f;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel);
	for (int32 i = 0; i < 9; ++i)
	{
		// the numpad's layout: 7 8 9 on top, 1 2 3 at the bottom
		const int32 Col = bRow ? i : i % 3;
		const int32 Row = bRow ? 0 : 2 - i / 3;
		Grid->AddSlot(Col, Row)
		[
			SNew(SMRSlot, Ui, FMRSlotRef(EMRSlotArea::SpellBar, i)).Size(SlotPx).KeyLabel(FString::FromInt(i + 1))
		];
	}
	return SNew(SMRPanel, Ui).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(1.f)
	[
		Grid
	];
}

bool SMRHUDRoot::IsSpellBarHovered() const
{
	return SpellBar.IsValid() && SpellBar->IsHovered();
}

void SMRHUDRoot::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geo, Time, Dt);
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	if (!Style || !SpellBar.IsValid())
	{
		return;
	}
	// the spell bar is faint until wanted: hovered, the dialog open, or just used
	const bool bWanted = Ui->IsInventoryOpen() || IsSpellBarHovered() || Ui->Now() - Ui->SpellBarLastUsed() < 1.5;
	const float Target = bWanted ? 1.f : Style->Number(TEXT("spellbar_idle_opacity"), 0.45f);
	SpellBarOpacity = FMath::FInterpTo(SpellBarOpacity, Target, Dt, 10.f);
	SpellBar->SetRenderOpacity(SpellBarOpacity);
}

void SMRHUDRoot::SetInventoryTab(int32 Tab)
{
	if (Inventory.IsValid())
	{
		Inventory->SetTab(static_cast<EMRInventoryTab>(FMath::Clamp(Tab, 0, static_cast<int32>(EMRInventoryTab::Count) - 1)));
	}
}

void SMRHUDRoot::OpenChat()
{
	if (ChatLog.IsValid())
	{
		ChatLog->OpenInput();
	}
}

void SMRHUDRoot::SetGameMenuOpen(bool bOpen)
{
	if (GameMenu.IsValid())
	{
		GameMenu->SetVisibility(bOpen ? EVisibility::Visible : EVisibility::Collapsed);
		if (bOpen)
		{
			GameMenu->OnOpened();
		}
	}
}

void SMRHUDRoot::SetInventoryOpen(bool bOpen)
{
	if (Inventory.IsValid())
	{
		Inventory->SetVisibility(bOpen ? EVisibility::Visible : EVisibility::Collapsed);
		if (bOpen)
		{
			Inventory->OnOpened();
		}
	}
	if (HotbarArea.IsValid())
	{
		// the dialog has its own copy of the hotbar (as Minecraft)
		HotbarArea->SetVisibility(bOpen ? EVisibility::Hidden : EVisibility::SelfHitTestInvisible);
	}
}
