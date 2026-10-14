#include "UI/SMRHUDFrames.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "Player/MRPlayerState.h"
#include "Rendering/DrawElements.h"
#include "UI/MRGameData.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMREnchantments.h"
#include "UI/SMRMinimap.h"
#include "UI/SMRSlot.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Zones/MRZoneSubsystem.h"

#define LOCTEXT_NAMESPACE "MRHUDFrames"

namespace
{
	UMRNetWorldSubsystem* NetWorldOf(const UMRUISubsystem* Ui)
	{
		const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
		UMRNetWorldSubsystem* W = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
		return W && W->IsActive() ? W : nullptr;
	}

	/** A label in the HUD: Heidelberg (or the UI font) with Shards' shadow (1px 1px 0 black, 0 0 4px black). */
	TSharedRef<STextBlock> HudLabel(UMRUIStyle* S, const TAttribute<FText>& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(Font)
			.ColorAndOpacity(Color)
			.ShadowOffset(FVector2D(1.0, 1.0) * S->HudPx())
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	}

	/** Text cut to a width with an ellipsis (Shards' text-overflow: ellipsis). */
	FString Ellipsize(const FString& Text, const FSlateFontInfo& Font, float Width)
	{
		if (MRPaint::MeasureText(Text, Font).X <= Width)
		{
			return Text;
		}
		FString Cut = Text;
		while (Cut.Len() > 1 && MRPaint::MeasureText(Cut + TEXT("..."), Font).X > Width)
		{
			Cut.LeftChopInline(1);
		}
		return Cut.TrimEnd() + TEXT("...");
	}

	// Shards' .hud-bars (Sidebar.tsx HudBars, styles.css .main-stat, .stat-icon, .stat-bar): padding 3,
	// rows 15 px a gap of 2 apart, the 17 x 15 icon, a gap of 3, the 13 px bar
	constexpr float BarsPad = 3.f, RowH = 15.f, RowGap = 2.f, IconW = 17.f, IconH = 15.f, IconGap = 3.f, BarH = 13.f;
	constexpr int32 NumVitals = 3;

	// Shards' quick emotes: the command, the label; a null command is the divider
	struct FEmote
	{
		const TCHAR* Command;
		FText Label;
	};
	const TArray<FEmote>& Emotes()
	{
		static const TArray<FEmote> List = {
			{TEXT("wave"), LOCTEXT("Wave", "Wave")}, {TEXT("point"), LOCTEXT("Point", "Point")}, {TEXT("dance"), LOCTEXT("Dance", "Dance")},
			{nullptr, FText::GetEmpty()},
			{TEXT("happy"), LOCTEXT("Happy", "Happy")}, {TEXT("sad"), LOCTEXT("Sad", "Sad")}, {TEXT("neutral"), LOCTEXT("Neutral", "Neutral")},
			{TEXT("wry"), LOCTEXT("Wry", "Wry")}};
		return List;
	}
	// .quick-emotes: padding 5 6, gap 4; its buttons 13 px text, padding 3 9; the divider 1 px with 3 px either side
	constexpr float EmPadX = 6.f, EmPadY = 5.f, EmGap = 4.f, EmBtnPadX = 9.f, EmBtnPadY = 3.f, EmText = 13.f, EmDivider = 7.f;
}

// ------------------------------------------------------------------------------ SMRVitalBars

void SMRVitalBars::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetToolTipText(TAttribute<FText>::CreateLambda([this]()
	{
		// the numbers, and the cap health and mana run to (the bars show the value only)
		TArray<FString> Lines;
		const FText Names[] = {LOCTEXT("Health", "Health"), LOCTEXT("Mana", "Mana"), LOCTEXT("Vigor", "Vigor")};
		for (int32 i = 0; i < NumVitals && UI.IsValid(); ++i)
		{
			float Value = 0.f, Max = 0.f, Limit = 0.f;
			if (UI->GetVitalBar(i, Value, Max, Limit))
			{
				Lines.Add(FString::Printf(TEXT("%s %d / %d"), *Names[i].ToString(), FMath::RoundToInt(Value), FMath::RoundToInt(i == 2 ? Max : Limit)));
			}
		}
		return FText::FromString(FString::Join(Lines, TEXT("\n")));
	}));
}

FVector2D SMRVitalBars::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float Px = S ? S->HudPx() : 1.f;
	const float W = S ? S->HudNumber(TEXT("bars_w"), 200.f) : 200.f;
	return FVector2D(W, BarsPad * 2.f + RowH * NumVitals + RowGap * (NumVitals - 1)) * Px;
}

void SMRVitalBars::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	for (int32 i = 0; i < NumVitals; ++i)
	{
		float Value = 0.f, Max = 0.f, Limit = 0.f;
		const float Target = UI.IsValid() && UI->GetVitalBar(i, Value, Max, Limit) && Max > 0.f ? FMath::Clamp(Value / Max, 0.f, 1.f) : 0.f;
		Shown[i] = Shown[i] < 0.f ? Target : FMath::FInterpTo(Shown[i], Target, Dt, 12.f);
	}
}

int32 SMRVitalBars::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const float Px = S->HudPx();
	const FSlateBrush* W = S->White();
	// the faint box (rgba(0, 0, 0, 0.35), its corners rounded 3 px)
	MRPaint::Rounded(Out, Layer, Geo, FVector2f::ZeroVector, Size, S->HudColor(TEXT("bars_bg"), FLinearColor(0.f, 0.f, 0.f, 0.35f)) * Tint, 3.f * Px);

	// the original's bar frame (InterfaceDrawBarBorder): its 5 x 17 ends, 2 px above and below and 5 px
	// out either side, and its top and bottom strips across the bar
	const float ArtToHud = Px / FMath::Max(0.01f, S->Px());  // a piece's size is in the dialogs' units: in HUD pixels
	const FVector2f CapL = S->PieceSize(TEXT("bar_left")) * ArtToHud, CapR = S->PieceSize(TEXT("bar_right")) * ArtToHud;
	const float TopH = S->PieceSize(TEXT("bar_top")).Y * ArtToHud, BottomH = S->PieceSize(TEXT("bar_bottom")).Y * ArtToHud;
	const FSlateBrush* Top = S->Brush(TEXT("bar_top"), true, ArtToHud);
	const FSlateBrush* Bottom = S->Brush(TEXT("bar_bottom"), true, ArtToHud);
	const FSlateBrush* Left = S->Brush(TEXT("bar_left"), false, ArtToHud);
	const FSlateBrush* Right = S->Brush(TEXT("bar_right"), false, ArtToHud);

	const FLinearColor FillColor = S->HudColor(TEXT("stat_fill"), FLinearColor(0.f, 0.216f, 0.f));
	const FLinearColor LowColor = S->HudColor(TEXT("stat_low"), FLinearColor(1.f, 0.f, 0.f));
	const FLinearColor LimitColor = S->HudColor(TEXT("stat_limit"), FLinearColor(0.216f, 0.f, 0.f));
	const FLinearColor EmptyColor = S->HudColor(TEXT("stat_bg"), FLinearColor(0.03f, 0.f, 0.f));
	const FSlateFontInfo Font = S->HudFont(S->HudNumber(TEXT("stat_text_size"), 10.f), true);
	for (int32 i = 0; i < NumVitals; ++i)
	{
		const float Y = (BarsPad + i * (RowH + RowGap)) * Px;
		// the stat's icon (user.kod user_stat_health / _mana / _vigor)
		if (const FSlateBrush* Icon = Ui->VitalIcon(i))
		{
			const FVector2f Box(IconW * Px, IconH * Px);
			const FVector2f Tex(Icon->ImageSize);
			const float K = FMath::Min(Box.X / FMath::Max(1.f, Tex.X), Box.Y / FMath::Max(1.f, Tex.Y));
			const FVector2f Fit = Tex * K;
			MRPaint::Box(Out, Layer + 1, Geo, Icon, FVector2f(BarsPad * Px, Y) + (Box - Fit) * 0.5f, Fit, Tint);
		}
		const FVector2f BarPos((BarsPad + IconW + IconGap) * Px, Y + (RowH - BarH) * 0.5f * Px);
		const FVector2f BarSize(Size.X - BarPos.X - BarsPad * Px, BarH * Px);
		MRPaint::Box(Out, Layer + 1, Geo, W, BarPos, BarSize, EmptyColor * Tint);
		float Value = 0.f, Max = 0.f, Limit = 0.f;
		if (Ui->GetVitalBar(i, Value, Max, Limit) && Max > 0.f)
		{
			// graphctl.c: the value in green (red while vigor is under MIN_VIGOR: no running), what it could fill to in dark red
			const float Fill = BarSize.X * FMath::Max(0.f, Shown[i]);
			const float LimitW = BarSize.X * FMath::Clamp(Limit / Max, 0.f, 1.f);
			if (LimitW > Fill + 0.5f)
			{
				MRPaint::Box(Out, Layer + 2, Geo, W, BarPos + FVector2f(Fill, 0.f), FVector2f(LimitW - Fill, BarSize.Y), LimitColor * Tint);
			}
			const bool bLow = i == 2 && Value < S->HudNumber(TEXT("min_vigor"), 10.f);
			if (Fill > 0.f)
			{
				MRPaint::Box(Out, Layer + 3, Geo, W, BarPos, FVector2f(Fill, BarSize.Y), (bLow ? LowColor : FillColor) * Tint);
			}
			// the number: inside at the left once the bar is over 70 % full, else just past the fill
			const FString Text = FString::FromInt(FMath::RoundToInt(Value));
			const FVector2f M = MRPaint::MeasureText(Text, Font);
			const float TX = Shown[i] > 0.7f ? 2.f * Px : Fill + 2.f * Px;
			MRPaint::Text(Out, Layer + 6, Geo, Text, Font, BarPos + FVector2f(TX, (BarSize.Y - M.Y) * 0.5f), FLinearColor::White * Tint, 0.f);
		}
		MRPaint::Tile(Out, Layer + 4, Geo, Top, BarPos + FVector2f(0.f, -2.f * Px), FVector2f(BarSize.X, TopH), Tint);
		MRPaint::Tile(Out, Layer + 4, Geo, Bottom, BarPos + FVector2f(0.f, BarSize.Y + 2.f * Px - BottomH), FVector2f(BarSize.X, BottomH), Tint);
		MRPaint::Box(Out, Layer + 5, Geo, Left, BarPos + FVector2f(-CapL.X, -2.f * Px), CapL, Tint);
		MRPaint::Box(Out, Layer + 5, Geo, Right, BarPos + FVector2f(BarSize.X, -2.f * Px), CapR, Tint);
	}
	const UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	if (NetWorld && NetWorld->IsChoosingTarget())
	{
		// picking a spell's target: a click here picks us (Shards' .hud-bars.selecting outline)
		MRPaint::Rounded(Out, Layer + 7, Geo, FVector2f(-2.f * Px, -2.f * Px), Size + FVector2f(4.f * Px, 4.f * Px), FLinearColor::Transparent, 4.f * Px,
			S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f)) * Tint, Px);
	}
	return Layer + 8;
}

FReply SMRVitalBars::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	const UMRNetSubsystem* Net = Ui ? Ui->GetNet() : nullptr;
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && NetWorld && Net && NetWorld->IsChoosingTarget())
	{
		NetWorld->ChooseTarget(Net->GetPlayer().Id);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRToolButton

void SMRToolButton::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Piece = InArgs._Piece;
	bPressed = InArgs._bPressed;
	OnClicked = InArgs._OnClicked;
	SetToolTipText(InArgs._ToolTip);
}

FVector2D SMRToolButton::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	return S ? FVector2D(S->HudNumber(TEXT("button_w"), 24.f), S->HudNumber(TEXT("button_h"), 20.f)) * S->HudPx() : FVector2D(24.f, 20.f);
}

int32 SMRToolButton::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!S)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	// toolbar.c ToolbarDrawButton: the right half while held down or toggled in
	const bool bIn = bDown || bPressed.Get(false);
	if (const FSlateBrush* B = S->Brush(FName(*FString::Printf(TEXT("%s_%s"), *Piece.ToString(), bIn ? TEXT("down") : TEXT("up")))))
	{
		MRPaint::Box(Out, Layer, Geo, B, FVector2f::ZeroVector, Size, Tint);
	}
	else
	{
		MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Size, 2.f * S->HudPx());
	}
	if (IsHovered() && !bIn)
	{
		MRPaint::Box(Out, Layer + 2, Geo, S->White(), FVector2f::ZeroVector, Size, FLinearColor(1.f, 1.f, 1.f, 0.1f) * Tint);
	}
	return Layer + 3;
}

FReply SMRToolButton::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bDown = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMRToolButton::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!bDown || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bDown = false;
	if (Geo.IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		OnClicked.ExecuteIfBound();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SMRToolButton::OnMouseLeave(const FPointerEvent& Event)
{
	SLeafWidget::OnMouseLeave(Event);
	if (!HasMouseCapture())
	{
		bDown = false;
	}
}

FCursorReply SMRToolButton::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

// ------------------------------------------------------------------------------ SMRRoundButton

void SMRRoundButton::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Label = InArgs._Label;
	Piece = InArgs._Piece;
	bPressed = InArgs._bPressed;
	Size = InArgs._Size;
	OnClicked = InArgs._OnClicked;
	SetToolTipText(InArgs._ToolTip);
}

FVector2D SMRRoundButton::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	return FVector2D(Size, Size) * (S ? S->HudPx() : 1.f);
}

int32 SMRRoundButton::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!S)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Box(Geo.GetLocalSize());
	const float Px = S->HudPx();
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	const bool bOn = bPressed.Get(false);
	const FLinearColor Edge = IsHovered() || bOn ? Gold : S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f));
	MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Box, Box.X * 0.5f, Tint.A, &Edge);
	if (bOn)
	{
		// Shards' .map-tool[aria-pressed]: box-shadow 0 0 6px gold
		MRPaint::Rounded(Out, Layer + 2, Geo, FVector2f(-2.f * Px, -2.f * Px), Box + FVector2f(4.f * Px, 4.f * Px), FLinearColor::Transparent,
			Box.X * 0.5f + 2.f * Px, Gold * FLinearColor(1.f, 1.f, 1.f, 0.45f) * Tint, 2.f * Px);
	}
	if (!Piece.IsNone())
	{
		// the toolbar's art (its up half), 20 x 17 in the middle (Shards: background-size 20px 17px, pixelated)
		if (const FSlateBrush* B = S->Brush(FName(*(Piece.ToString() + TEXT("_up")))))
		{
			const FVector2f Art(S->HudNumber(TEXT("map_tool_art_w"), 20.f) * Px, S->HudNumber(TEXT("map_tool_art_h"), 17.f) * Px);
			MRPaint::Box(Out, Layer + 3, Geo, B, (Box - Art) * 0.5f, Art, Tint);
		}
		return Layer + 5;
	}
	const FSlateFontInfo Font = S->HudFont(16.f, true);
	const FVector2f M = MRPaint::MeasureText(Label, Font);
	MRPaint::Text(Out, Layer + 3, Geo, Label, Font, (Box - M) * 0.5f, (IsHovered() ? Gold : S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f))) * Tint,
		Px);
	return Layer + 5;
}

FReply SMRRoundButton::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}

FCursorReply SMRRoundButton::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

// ------------------------------------------------------------------------------ SMRPortrait

void SMRPortrait::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Face.DrawAs = ESlateBrushDrawType::NoDrawType;
	SetToolTipText(LOCTEXT("FaceTip", "You. Click for the emotes (Q); right click to look at yourself."));
}

FVector2D SMRPortrait::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	return S ? FVector2D(S->HudNumber(TEXT("portrait_w"), 60.f), S->HudNumber(TEXT("portrait_h"), 84.f)) * S->HudPx() : FVector2D(60.f, 84.f);
}

void SMRPortrait::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	UMRUISubsystem* Ui = UI.Get();
	UObject* Target = Ui ? Ui->UpdateSelfPortrait() : nullptr;
	if (Target != Face.GetResourceObject())
	{
		Face = FSlateBrush();
		if (Target)
		{
			UMRUIStyle::SetImage(Face, Target, FVector2f(256.f, 256.f));
		}
		else
		{
			Face.DrawAs = ESlateBrushDrawType::NoDrawType;
		}
	}
}

int32 SMRPortrait::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	if (!S)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const float Px = S->HudPx();
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	const FLinearColor* Edge = IsHovered() || Ui->IsQuickEmotesOpen() ? &Gold : nullptr;
	MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Size, 4.f * Px, Tint.A, Edge);
	if (Face.GetResourceObject())
	{
		// the face, 52 x 78 in the middle: the square portrait's middle two thirds across
		const FVector2f FaceSize = FVector2f(S->HudNumber(TEXT("face_w"), 52.f), S->HudNumber(TEXT("face_h"), 78.f)) * Px;
		const float U = FMath::Clamp(FaceSize.X / FaceSize.Y, 0.f, 1.f);
		FSlateBrush B = Face;
		B.SetUVRegion(FBox2f(FVector2f(0.5f - U * 0.5f, 0.f), FVector2f(0.5f + U * 0.5f, 1.f)));
		MRPaint::Box(Out, Layer + 3, Geo, &B, (Size - FaceSize) * 0.5f, FaceSize, Tint);
	}
	const UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	if (NetWorld && NetWorld->IsChoosingTarget())
	{
		// picking a spell's target: a click here picks us (Shards' dashed outline)
		MRPaint::Rounded(Out, Layer + 4, Geo, FVector2f(-3.f * Px, -3.f * Px), Size + FVector2f(6.f * Px, 6.f * Px), FLinearColor::Transparent, 6.f * Px,
			Gold * Tint, Px);
	}
	return Layer + 5;
}

FReply SMRPortrait::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	// userarea.c UserAreaProc: a click picks us as a spell's target; a right click looks at us.
	// Ours (Shards): otherwise a click shows the emotes
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui)
	{
		return FReply::Unhandled();
	}
	UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	const UMRNetSubsystem* Net = Ui->GetNet();
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (NetWorld && Net)
		{
			Ui->LookAt(Net->GetPlayer().Id);
		}
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	if (NetWorld && Net && NetWorld->IsChoosingTarget())
	{
		NetWorld->ChooseTarget(Net->GetPlayer().Id);
		return FReply::Handled();
	}
	Ui->ToggleQuickEmotes();
	return FReply::Handled();
}

FCursorReply SMRPortrait::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

// ------------------------------------------------------------------------------ SMRUnitFrame

void SMRUnitFrame::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	// Shards' .unit-frame: the face, 8 px, a 200 px column of the bars and our enchantments under them (6 px apart)
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	const float SideW = S->HudNumber(TEXT("bars_w"), 200.f);
	Face = SNew(SMRPortrait, InUI);
	Bars = SNew(SMRVitalBars, InUI);
	Enchantments = SNew(SMREnchantments, InUI).bHud(true).IconPx(S->HudNumber(TEXT("enchant_px"), 26.f)).WrapWidth(SideW);
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		[
			Face.ToSharedRef()
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8.f * Px, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(SideW * Px)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Bars.ToSharedRef()
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 6.f * Px, 0.f, 0.f)
				[
					Enchantments.ToSharedRef()
				]
			]
		]
	];
}

// ------------------------------------------------------------------------------ SMRTargetFrame

namespace
{
	// Shards' .target-frame: padding 4 6 4 4, gap 8, the 32 px picture, the x 20 px
	constexpr float PadL = 4.f, PadR = 6.f, PadV = 4.f, TargetGap = 8.f, ClearSide = 20.f;
}

void SMRTargetFrame::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	MaxWidth = InArgs._MaxWidth;
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return Target() ? EVisibility::Visible : EVisibility::Collapsed; }));
}

const FMRNetObject* SMRTargetFrame::Target() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	const UMRNetSubsystem* Net = Ui ? Ui->GetNet() : nullptr;
	const uint32 Id = NetWorld ? NetWorld->GetTargetId() : 0;
	return Id && Net ? Net->FindObject(Id) : nullptr;
}

bool SMRTargetFrame::IsAttackable(const FMRNetObject& O) const
{
	const UMRNetSubsystem* Net = UI.IsValid() ? UI->GetNet() : nullptr;
	return (O.Flags & MRMsg::OF_ATTACKABLE) != 0 && (!Net || O.Id != Net->GetPlayer().Id);
}

FVector2D SMRTargetFrame::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const FMRNetObject* O = Target();
	if (!S || !O)
	{
		return FVector2D::ZeroVector;
	}
	const float Px = S->HudPx();
	const float Icon = S->HudNumber(TEXT("target_icon"), 32.f);
	const FVector2f Name = MRPaint::MeasureText(O->Name, S->TitleFont(S->HudNumber(TEXT("target_name_size"), 18.f)));
	const float W = (PadL + Icon + TargetGap + TargetGap + ClearSide + PadR) * Px + Name.X;
	const float H = (PadV * 2.f + Icon) * Px;
	const float Max = MaxWidth.IsSet() ? MaxWidth.Get() : 0.f;
	return FVector2D(Max > 0.f ? FMath::Min(W, Max) : W, H);
}

FVector2f SMRTargetFrame::ClearButtonPos(const FVector2f& Size) const
{
	const float Px = UI.IsValid() && UI->GetStyle() ? UI->GetStyle()->HudPx() : 1.f;
	return FVector2f(Size.X - (PadR + ClearSide) * Px, (Size.Y - ClearSide * Px) * 0.5f);
}

int32 SMRTargetFrame::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	const FMRNetObject* O = Target();
	if (!S || !O)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const float Px = S->HudPx();
	const bool bAttackable = IsAttackable(*O);
	const FLinearColor Edge = bAttackable ? S->HudColor(TEXT("attackable_edge"), FLinearColor(0.72f, 0.05f, 0.03f, 0.8f))
		: S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f));
	MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Size, 4.f * Px, Tint.A, &Edge);

	// its picture: its own bitmap in the group it shows (as the inventory's icons), fitted to the box
	const float Icon = S->HudNumber(TEXT("target_icon"), 32.f) * Px;
	const int32 Group = O->Animation.Type == MRMsg::ANIMATE_NONE ? O->Animation.Group : O->Animation.GroupLow;
	if (const FSlateBrush* B = Ui->BitmapIcon(O->Icon, FMath::Max(1, Group)))
	{
		const FVector2f Tex(B->ImageSize);
		const float K = Icon / FMath::Max(1.f, FMath::Max(Tex.X, Tex.Y));
		const FVector2f Fit = Tex * K;
		MRPaint::Box(Out, Layer + 3, Geo, B, FVector2f(PadL * Px, PadV * Px) + (FVector2f(Icon, Icon) - Fit) * 0.5f, Fit, Tint);
	}

	// its name, red if it can be attacked
	const FSlateFontInfo Font = S->TitleFont(S->HudNumber(TEXT("target_name_size"), 18.f));
	const float NameX = (PadL + TargetGap) * Px + Icon;
	const float NameW = ClearButtonPos(Size).X - TargetGap * Px - NameX;
	const FString Name = Ellipsize(O->Name, Font, NameW);
	const FVector2f M = MRPaint::MeasureText(Name, Font);
	const FLinearColor NameColor = bAttackable ? S->HudColor(TEXT("attackable_text"), FLinearColor(1.f, 0.16f, 0.1f))
		: S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f));
	MRPaint::Text(Out, Layer + 3, Geo, Name, Font, FVector2f(NameX, (Size.Y - M.Y) * 0.5f), NameColor * Tint, Px);

	// the x that clears it
	const FSlateFontInfo XFont = S->HudFont(16.f, true);
	const FString X = TEXT("×");
	const FVector2f XM = MRPaint::MeasureText(X, XFont);
	const FVector2f XAt = ClearButtonPos(Size);
	MRPaint::Text(Out, Layer + 3, Geo, X, XFont, XAt + (FVector2f(ClearSide, ClearSide) * Px - XM) * 0.5f,
		S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f)) * Tint, 0.f);
	return Layer + 5;
}

FReply SMRTargetFrame::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	const FMRNetObject* O = Target();
	if (!NetWorld || !O)
	{
		return FReply::Unhandled();
	}
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Ui->LookAt(O->Id);
		return FReply::Handled();
	}
	const FVector2f Local = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FVector2f At = ClearButtonPos(FVector2f(Geo.GetLocalSize()));
	const float Side = ClearSide * Ui->GetStyle()->HudPx();
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Local.X >= At.X && Local.X <= At.X + Side)
	{
		NetWorld->ClearTarget();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRQuickEmotes

void SMRQuickEmotes::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return UI.IsValid() && UI->IsQuickEmotesOpen() ? EVisibility::Visible : EVisibility::Collapsed;
	}));
}

void SMRQuickEmotes::Layout(TArray<FBox2f>& Out) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	Out.Reset();
	if (!S)
	{
		return;
	}
	const float Px = S->HudPx();
	const FSlateFontInfo Font = S->HudFont(EmText);
	float X = EmPadX * Px;
	const float H = MRPaint::MeasureText(TEXT("Wg"), Font).Y + 2.f * EmBtnPadY * Px;
	for (const FEmote& E : Emotes())
	{
		const float W = E.Command ? MRPaint::MeasureText(E.Label.ToString(), Font).X + 2.f * EmBtnPadX * Px : EmDivider * Px;
		Out.Add(FBox2f(FVector2f(X, EmPadY * Px), FVector2f(X + W, EmPadY * Px + H)));
		X += W + EmGap * Px;
	}
}

FVector2D SMRQuickEmotes::ComputeDesiredSize(float) const
{
	TArray<FBox2f> Boxes;
	Layout(Boxes);
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (Boxes.IsEmpty() || !S)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(Boxes.Last().Max.X + EmPadX * S->HudPx(), Boxes.Last().Max.Y + EmPadY * S->HudPx());
}

int32 SMRQuickEmotes::ButtonAt(const FVector2f& Local) const
{
	TArray<FBox2f> Boxes;
	Layout(Boxes);
	for (int32 i = 0; i < Boxes.Num(); ++i)
	{
		if (Emotes()[i].Command && Boxes[i].IsInside(Local))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 SMRQuickEmotes::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!S)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const float Px = S->HudPx();
	const FVector2f Size(Geo.GetLocalSize());
	const FLinearColor Edge = S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f));
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	MRPaint::SoftShadow(Out, Layer, Geo, FVector2f::ZeroVector, Size, 4.f * Px, 2.f * Px, 8.f * Px, FLinearColor(0.f, 0.f, 0.f, 0.6f) * Tint);
	MRPaint::Rounded(Out, Layer + 1, Geo, FVector2f::ZeroVector, Size, S->HudColor(TEXT("emote_panel"), FLinearColor(0.011f, 0.0075f, 0.006f, 0.88f)) * Tint,
		4.f * Px, Edge * Tint, Px);
	TArray<FBox2f> Boxes;
	Layout(Boxes);
	const FSlateFontInfo Font = S->HudFont(EmText);
	for (int32 i = 0; i < Boxes.Num(); ++i)
	{
		const FEmote& E = Emotes()[i];
		const FBox2f& B = Boxes[i];
		if (!E.Command)
		{
			// the divider: a 1 px line, 2 px in from the top and bottom
			MRPaint::Box(Out, Layer + 2, Geo, S->White(), FVector2f(B.GetCenter().X - 0.5f * Px, B.Min.Y + 2.f * Px), FVector2f(Px, B.GetSize().Y - 4.f * Px), Edge * Tint);
			continue;
		}
		const bool bHot = i == Hovered;
		MRPaint::Rounded(Out, Layer + 2, Geo, B.Min, B.GetSize(), S->HudColor(TEXT("emote_button"), FLinearColor(0.023f, 0.015f, 0.009f)) * Tint, 3.f * Px,
			(bHot ? Gold : Edge) * Tint, Px);
		const FString Label = E.Label.ToString();
		const FVector2f M = MRPaint::MeasureText(Label, Font);
		MRPaint::Text(Out, Layer + 3, Geo, Label, Font, B.Min + (B.GetSize() - M) * 0.5f,
			(bHot ? Gold : S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f))) * Tint, 0.f);
	}
	return Layer + 4;
}

FReply SMRQuickEmotes::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !UI.IsValid())
	{
		return FReply::Handled();
	}
	const int32 I = ButtonAt(Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (I != INDEX_NONE)
	{
		UI->RunEmote(FName(Emotes()[I].Command));
	}
	return FReply::Handled();
}

FReply SMRQuickEmotes::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	Hovered = ButtonAt(Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	return FReply::Unhandled();
}

void SMRQuickEmotes::OnMouseLeave(const FPointerEvent& Event)
{
	SLeafWidget::OnMouseLeave(Event);
	Hovered = INDEX_NONE;
}

FCursorReply SMRQuickEmotes::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return Hovered != INDEX_NONE ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRMapCluster

void SMRMapCluster::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	const float Side = S->HudNumber(TEXT("map_side"), 172.f);
	const float Button = S->HudNumber(TEXT("map_button"), 26.f);
	const float Tool = S->HudNumber(TEXT("map_tool"), 30.f);
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	TWeakObjectPtr<UMRUISubsystem> WeakUI(InUI);
	auto Online = [WeakUI]() { return NetWorldOf(WeakUI.Get()) ? EVisibility::Visible : EVisibility::Collapsed; };
	// a click on the map opens the large map (and M); the large map closes itself
	Map = SNew(SMRMinimap, InUI).bRound(true).Side(Side)
		.OnClicked_Lambda([WeakUI](FVector2D) { if (WeakUI.IsValid()) WeakUI->ToggleWindow(EMRWindow::Map); });
	// the toolbar's Rest/Stand (command.c CommandRest / CommandStand: in while resting) and the mailbox, at the map's top corners
	TSharedRef<SWidget> Rest = SNew(SMRRoundButton, InUI).Piece(TEXT("btn_rest")).Size(Tool).ToolTip(LOCTEXT("RestStand", "Rest / Stand (R)"))
		.Visibility_Lambda(Online)
		.bPressed_Lambda([WeakUI]() { const UMRNetWorldSubsystem* W = NetWorldOf(WeakUI.Get()); return W && W->IsResting(); })
		.OnClicked_Lambda([WeakUI]()
		{
			if (UMRNetWorldSubsystem* W = NetWorldOf(WeakUI.Get()))
			{
				W->SetResting(!W->IsResting());
			}
		});
	Tools = Rest;
	RoomLabel = HudLabel(S, TAttribute<FText>::CreateSP(this, &SMRMapCluster::RoomName), S->TitleFont(S->HudNumber(TEXT("room_name_size"), 16.f)), Gold);
	Enchantments = SNew(SMREnchantments, InUI).bRoom(true).bHud(true).IconPx(S->HudNumber(TEXT("enchant_px"), 26.f))
		.WrapWidth(S->HudNumber(TEXT("map_width"), 184.f)).bCentre(true);
	ChildSlot
	[
		SNew(SBox).WidthOverride(S->HudNumber(TEXT("map_width"), 184.f) * Px)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					Map.ToSharedRef()
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(-2.f * Px, 0.f, 0.f, -2.f * Px)
				[
					SNew(SMRRoundButton, InUI).Label(TEXT("−")).Size(Button).ToolTip(LOCTEXT("ZoomOut", "Zoom out (-)"))
						.OnClicked_Lambda([WeakUI]() { if (WeakUI.IsValid()) WeakUI->OnMapZoom(-1.f); })
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, -2.f * Px, -2.f * Px)
				[
					SNew(SMRRoundButton, InUI).Label(TEXT("+")).Size(Button).ToolTip(LOCTEXT("ZoomIn", "Zoom in (=)"))
						.OnClicked_Lambda([WeakUI]() { if (WeakUI.IsValid()) WeakUI->OnMapZoom(1.f); })
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(-2.f * Px, -2.f * Px, 0.f, 0.f)
				[
					Rest
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, -2.f * Px, -2.f * Px, 0.f)
				[
					SNew(SMRRoundButton, InUI).Piece(TEXT("btn_mailbox")).Size(Tool).ToolTip(LOCTEXT("Mail", "Read mail (L)"))
						.Visibility_Lambda(Online)
						.OnClicked_Lambda([WeakUI]() { if (WeakUI.IsValid()) WeakUI->SetWindowOpen(EMRWindow::Mail, true); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f * Px, 0.f, 0.f)
			[
				RoomLabel.ToSharedRef()
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f * Px, 0.f, 0.f)
			[
				Enchantments.ToSharedRef()
			]
		]
	];
}

FText SMRMapCluster::RoomName() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRNetSubsystem* Net = Ui ? Ui->GetNet() : nullptr;
	if (NetWorldOf(Ui) && Net && !Net->GetPlayer().RoomName.IsEmpty())
	{
		return FText::FromString(Net->GetPlayer().RoomName);
	}
	const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	const UMRZoneSubsystem* Zones = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return FText::GetEmpty();
	}
	int32 Rid = 0;
	if (const AMRPlayerState* PS = PC->GetPlayerState<AMRPlayerState>())
	{
		Rid = PS->GetZoneId();
	}
	if (!Rid && PC->GetPawn())
	{
		Rid = Zones->ZoneAtLocation(PC->GetPawn()->GetActorLocation());
	}
	const FMRZoneInfo* Zone = Zones->FindZone(Rid);
	return Zone ? FText::FromString(Zone->Name) : FText::GetEmpty();
}

// ------------------------------------------------------------------------------ SMRActionBar

void SMRActionBar::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	const float Pad = S->HudNumber(TEXT("action_pad"), 6.f) * Px;
	const float Gap = S->HudNumber(TEXT("action_gap"), 4.f) * Px;
	const float SlotPx = S->HudNumber(TEXT("slot"), 44.f);
	const float SlotGap = S->HudNumber(TEXT("slot_gap"), 4.f) * Px;

	auto Row = [&](int32 RowIndex)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 i = 0; i < FMRQuickSlot::PerRow; ++i)
		{
			const int32 Index = RowIndex * FMRQuickSlot::PerRow + i;
			Box->AddSlot().AutoWidth().Padding(i > 0 ? SlotGap : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMRSlot, InUI, FMRSlotRef(EMRSlotArea::QuickSlot, Index)).bHud(true).Size(SlotPx).KeyLabel(UMRUISubsystem::QuickSlotKeyLabel(Index))
			];
		}
		return Box;
	};
	// the second row (the numpad's) over the first, as Shards' Second Hotbar (Options > Game)
	ChildSlot.Padding(Pad)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, Gap)
		[
			SNew(SBox).Visibility_Lambda([]() { return UMRUISubsystem::ShowsSecondHotbar() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Row(1)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(0)
		]
	];
}

int32 SMRActionBar::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (S)
	{
		MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()), 5.f * S->HudPx(), WStyle.GetColorAndOpacityTint().A);
	}
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 3, WStyle, bParentEnabled);
}

// ------------------------------------------------------------------------------ SMRQuickSlotPicker

namespace
{
	/** A row of the picker: an icon and a name; a click picks it. */
	class SPickerRow : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPickerRow) : _Icon(nullptr), _bChosen(false) {}
			SLATE_ARGUMENT(const FSlateBrush*, Icon)
			SLATE_ARGUMENT(FString, Text)
			SLATE_ARGUMENT(bool, bChosen)
			SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
		{
			UI = InUI;
			Icon = InArgs._Icon;
			Text = InArgs._Text;
			bChosen = InArgs._bChosen;
			OnClicked = InArgs._OnClicked;
		}
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			return FVector2D(260.f, 30.f) * (S ? S->HudPx() : 1.f);
		}
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (!S)
			{
				return Layer;
			}
			const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
			const FVector2f Size(Geo.GetLocalSize());
			const float Px = S->HudPx();
			const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
			if (IsHovered() || bChosen)
			{
				MRPaint::Rounded(Out, Layer, Geo, FVector2f::ZeroVector, Size, Gold * FLinearColor(1.f, 1.f, 1.f, bChosen ? 0.22f : 0.12f) * Tint, 3.f * Px);
			}
			const float IconSide = 24.f * Px;
			if (Icon)
			{
				MRPaint::Box(Out, Layer + 1, Geo, Icon, FVector2f(3.f * Px, (Size.Y - IconSide) * 0.5f), FVector2f(IconSide, IconSide), Tint);
			}
			const FSlateFontInfo Font = S->HudFont(13.f);
			const FVector2f M = MRPaint::MeasureText(Text, Font);
			MRPaint::Text(Out, Layer + 1, Geo, Text, Font, FVector2f(IconSide + 10.f * Px, (Size.Y - M.Y) * 0.5f),
				(IsHovered() || bChosen ? Gold : S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f))) * Tint, Px);
			return Layer + 2;
		}
		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				OnClicked.ExecuteIfBound();
			}
			return FReply::Handled();
		}
		virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const override
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
		const FSlateBrush* Icon = nullptr;
		FString Text;
		bool bChosen = false;
		FSimpleDelegate OnClicked;
	};
}

namespace
{
	/** The HUD's panel (MRPaint::HudPanel) behind its content, where the content stands. */
	class SHudPanelBox : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SHudPanelBox) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
		{
			UI = InUI;
			ChildSlot[InArgs._Content.Widget];
		}
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			if (UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr)
			{
				MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, FVector2f(Geo.GetLocalSize()), 5.f * S->HudPx(), WStyle.GetColorAndOpacityTint().A);
			}
			return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer + 3, WStyle, bParentEnabled);
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
	};
}

void SMRQuickSlotPicker::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return UI.IsValid() && UI->GetQuickSlotPicker() != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed;
	}));
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	List = SNew(SVerticalBox);
	Scroll->AddSlot()[List.ToSharedRef()];
	Panel = SNew(SHudPanelBox, InUI)
		[
			SNew(SBox).Padding(10.f * Px).MaxDesiredHeight(380.f * Px)
			[
				Scroll
			]
		];
	ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		Panel.ToSharedRef()
	];
}

void SMRQuickSlotPicker::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	const int32 Want = UI.IsValid() ? UI->GetQuickSlotPicker() : INDEX_NONE;
	if (Want != BuiltFor)
	{
		BuiltFor = Want;
		if (Want != INDEX_NONE)
		{
			Rebuild();
			FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
		}
	}
}

void SMRQuickSlotPicker::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	UMRInventorySource* Source = Ui ? Ui->GetSource() : nullptr;
	if (!S || !Source || !List.IsValid())
	{
		return;
	}
	List->ClearChildren();
	const float Px = S->HudPx();
	const int32 Index = BuiltFor;
	const FMRQuickSlot Current = Source->GetQuickSlot(Index);
	TWeakObjectPtr<UMRUISubsystem> WeakUI(Ui);
	auto Pick = [WeakUI, Index](const FMRQuickSlot& Q)
	{
		return FSimpleDelegate::CreateLambda([WeakUI, Index, Q]()
		{
			if (UMRUISubsystem* U = WeakUI.Get(); U && U->GetSource())
			{
				U->GetSource()->SetQuickSlot(Index, Q);
				U->CloseQuickSlotPicker();
			}
		});
	};
	auto Heading = [&](const FText& Text)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 6.f * Px, 0.f, 2.f * Px)
		[
			HudLabel(S, Text, S->TitleFont(16.f), S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f)))
		];
	};
	List->AddSlot().AutoHeight()
	[
		HudLabel(S, FText::Format(LOCTEXT("PickerTitle", "Quick slot {0}"), FText::FromString(UMRUISubsystem::QuickSlotKeyLabel(Index))),
			S->TitleFont(18.f), S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f)))
	];
	// the spells we know, by name
	TArray<TPair<FString, FMRSlotContent>> Spells;
	for (const FName& Spell : Source->GetKnownSpells())
	{
		const FMRSlotContent C = FMRSlotContent::Spell(Spell);
		Spells.Emplace(Ui->NameFor(C).ToString(), C);
	}
	Spells.Sort([](const TPair<FString, FMRSlotContent>& A, const TPair<FString, FMRSlotContent>& B) { return A.Key < B.Key; });
	if (Spells.Num() > 0)
	{
		Heading(LOCTEXT("Spells", "Spells"));
	}
	for (const TPair<FString, FMRSlotContent>& P : Spells)
	{
		const FMRQuickSlot Q = Source->QuickSlotFor(P.Value);
		List->AddSlot().AutoHeight()
		[
			SNew(SPickerRow, Ui).Icon(Ui->IconFor(P.Value)).Text(P.Key).bChosen(Q == Current).OnClicked(Pick(Q))
		];
	}
	// the items we carry (worn or in the bag), one row a kind; the shillings left out
	TArray<FMRQuickSlot> Seen;
	TArray<TPair<FString, FMRSlotContent>> Items;
	auto AddItem = [&](const FMRSlotContent& C)
	{
		if (C.IsEmpty() || C.bSpell)
		{
			return;
		}
		const FMRQuickSlot Q = Source->QuickSlotFor(C);
		if (Seen.Contains(Q) || Q.Name.Contains(TEXT("shilling"), ESearchCase::IgnoreCase))
		{
			return;
		}
		Seen.Add(Q);
		Items.Emplace(Q.Name.IsEmpty() ? Ui->NameFor(C).ToString() : Q.Name, C);
	};
	for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
	{
		AddItem(Source->Get(FMRSlotRef::Equip(static_cast<EMREquipSlot>(i))));
	}
	const int32 BagSlots = Source->NumSlots(EMRSlotArea::Bag);
	for (int32 i = 0; i < BagSlots; ++i)
	{
		AddItem(Source->Get(FMRSlotRef(EMRSlotArea::Bag, i)));
	}
	if (Items.Num() > 0)
	{
		Heading(LOCTEXT("Items", "Items"));
	}
	for (const TPair<FString, FMRSlotContent>& P : Items)
	{
		const FMRQuickSlot Q = Source->QuickSlotFor(P.Value);
		List->AddSlot().AutoHeight()
		[
			SNew(SPickerRow, Ui).Icon(Ui->IconFor(P.Value)).Text(P.Key).bChosen(Q == Current).OnClicked(Pick(Q))
		];
	}
	List->AddSlot().AutoHeight().Padding(0.f, 6.f * Px, 0.f, 0.f)
	[
		SNew(SPickerRow, Ui).Text(LOCTEXT("Empty", "Empty").ToString()).bChosen(Current.IsEmpty()).OnClicked(Pick(FMRQuickSlot()))
	];
}

int32 SMRQuickSlotPicker::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	return SCompoundWidget::OnPaint(Args, Geo, Culling, Out, Layer, WStyle, bParentEnabled);
}

FReply SMRQuickSlotPicker::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape && UI.IsValid())
	{
		UI->CloseQuickSlotPicker();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMRQuickSlotPicker::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	// a click outside the list closes it (its rows take their own clicks)
	if (UI.IsValid())
	{
		UI->CloseQuickSlotPicker();
	}
	return FReply::Handled();
}

// ------------------------------------------------------------------------------ SMRViewNote

void SMRViewNote::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SMRViewNote::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	FString Note;
	double Age = 0.0;
	if (!S || !Ui->GetHudNote(Note, Age))
	{
		return Layer;
	}
	// Shards' view-note: shown, then fading over the last 40 % of 1.5 s
	const float Seconds = S->HudNumber(TEXT("note_seconds"), 1.5f);
	const float T = static_cast<float>(Age) / FMath::Max(0.1f, Seconds);
	if (T >= 1.f)
	{
		return Layer;
	}
	const float Alpha = T < 0.6f ? 1.f : 1.f - (T - 0.6f) / 0.4f;
	const FSlateFontInfo Font = S->TitleFont(S->HudNumber(TEXT("note_size"), 22.f));
	const FVector2f M = MRPaint::MeasureText(Note, Font);
	const FVector2f Size(Geo.GetLocalSize());
	MRPaint::Text(Out, Layer, Geo, Note, Font, FVector2f((Size.X - M.X) * 0.5f, Size.Y * 0.18f),
		S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f)) * FLinearColor(1.f, 1.f, 1.f, Alpha) * WStyle.GetColorAndOpacityTint(), S->HudPx());
	return Layer + 2;
}

#undef LOCTEXT_NAMESPACE
