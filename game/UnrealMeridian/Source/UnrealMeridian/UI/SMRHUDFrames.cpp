#include "UI/SMRHUDFrames.h"

#include "Engine/World.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "Player/MRPlayerState.h"
#include "Rendering/DrawElements.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMREnchantments.h"
#include "UI/SMRMinimap.h"
#include "UI/SMRSlot.h"
#include "Widgets/Layout/SBox.h"
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
}

// ------------------------------------------------------------------------------ SMRHudBar

void SMRHudBar::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Vital = InArgs._Vital;
	Kind = InArgs._Kind;
	Height = InArgs._Height;
	TextSize = InArgs._TextSize;
	bText = InArgs._bText;
	if (!bText)
	{
		// the experience line has no text: its numbers on hover (with the cursor free)
		SetToolTipText(TAttribute<FText>::CreateLambda([this]()
		{
			float Value = 0.f, Max = 0.f, Limit = 0.f;
			return UI.IsValid() && UI->GetVitalBar(Vital, Value, Max, Limit)
				? FText::Format(LOCTEXT("XP", "{0} / {1} XP"), FText::AsNumber(FMath::RoundToInt(Value)), FText::AsNumber(FMath::RoundToInt(Max)))
				: FText::GetEmpty();
		}));
	}
}

FVector2D SMRHudBar::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	return FVector2D(40.f, Height) * (S ? S->HudPx() : 1.f);
}

void SMRHudBar::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	float Value = 0.f, Max = 0.f, Limit = 0.f;
	const float Target = UI.IsValid() && UI->GetVitalBar(Vital, Value, Max, Limit) && Max > 0.f ? FMath::Clamp(Value / Max, 0.f, 1.f) : 0.f;
	Shown = Shown < 0.f ? Target : FMath::FInterpTo(Shown, Target, Dt, 12.f);
}

int32 SMRHudBar::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
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
	const float Radius = (bText ? 3.f : 2.f) * Px;
	float Value = 0.f, Max = 0.f, Limit = 0.f;
	const bool bHas = Ui->GetVitalBar(Vital, Value, Max, Limit) && Max > 0.f;

	// the gold ring outside (box-shadow 0 0 0 1px), the dark bar with its black edge
	MRPaint::Rounded(Out, Layer, Geo, FVector2f(-Px, -Px), Size + FVector2f(2.f * Px, 2.f * Px), FLinearColor::Transparent, Radius + Px,
		S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f)) * Tint, Px);
	MRPaint::Rounded(Out, Layer + 1, Geo, FVector2f::ZeroVector, Size, S->HudColor(TEXT("bar_bg"), FLinearColor(0.015f, 0.004f, 0.004f)) * Tint, Radius);
	if (bHas)
	{
		const FVector2f In(Px, Px);
		const FVector2f InSize = Size - 2.f * In;
		const float Fill = InSize.X * FMath::Max(0.f, Shown);
		const float LimitFrac = FMath::Clamp(Limit / Max, 0.f, 1.f);
		if (LimitFrac * InSize.X > Fill + 0.5f)
		{
			MRPaint::Box(Out, Layer + 2, Geo, S->White(), In + FVector2f(Fill, 0.f), FVector2f(LimitFrac * InSize.X - Fill, InSize.Y),
				S->HudColor(TEXT("bar_limit"), FLinearColor(1.f, 1.f, 1.f, 0.12f)) * Tint);
		}
		if (Fill > 0.5f)
		{
			// statmain.c: below MIN_VIGOR you can't run, and the bar turns red
			const bool bLow = Vital == 2 && Value < S->HudNumber(TEXT("min_vigor"), 10.f);
			const FString Key = bLow ? TEXT("health") : Kind;
			const FLinearColor Top = S->HudColor(*(Key + TEXT("_top")), FLinearColor::White);
			const FLinearColor Bottom = S->HudColor(*(Key + TEXT("_bottom")), FLinearColor::Gray);
			// rounded where the bar is (its left end; the right too once it's full)
			const float R = FMath::Max(0.f, Radius - Px);
			const bool bFull = Fill >= InSize.X - 0.5f;
			TArray<FSlateGradientStop> Stops = {FSlateGradientStop(FVector2f(0.f, 0.f), Top * Tint), FSlateGradientStop(FVector2f(0.f, InSize.Y), Bottom * Tint)};
			FSlateDrawElement::MakeGradient(Out, Layer + 3, Geo.ToPaintGeometry(FVector2f(Fill, InSize.Y), FSlateLayoutTransform(In)), MoveTemp(Stops),
				Orient_Horizontal, ESlateDrawEffect::None, FVector4f(R, bFull ? R : 0.f, bFull ? R : 0.f, R));
		}
		if (bText)
		{
			const FString Text = FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Value), FMath::RoundToInt(Max));
			const FSlateFontInfo Font = S->HudFont(TextSize, true);
			const FVector2f M = MRPaint::MeasureText(Text, Font);
			MRPaint::Text(Out, Layer + 4, Geo, Text, Font, (Size - M) * 0.5f, FLinearColor::White * Tint, Px);
		}
	}
	return Layer + 6;
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
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	const FLinearColor Edge = IsHovered() ? Gold : S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f));
	MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Box, Box.X * 0.5f, Tint.A, &Edge);
	const FSlateFontInfo Font = S->HudFont(16.f, true);
	const FVector2f M = MRPaint::MeasureText(Label, Font);
	MRPaint::Text(Out, Layer + 3, Geo, Label, Font, (Box - M) * 0.5f, (IsHovered() ? Gold : S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f))) * Tint,
		S->HudPx());
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
	MRPaint::HudPanel(Out, Layer, Geo, S, FVector2f::ZeroVector, Size, 4.f * Px, Tint.A);
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
			S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f)) * Tint, Px);
	}
	return Layer + 5;
}

FReply SMRPortrait::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	// userarea.c UserAreaProc: a click picks us as a spell's target; a right click looks at us
	UMRUISubsystem* Ui = UI.Get();
	UMRNetWorldSubsystem* NetWorld = NetWorldOf(Ui);
	const UMRNetSubsystem* Net = Ui ? Ui->GetNet() : nullptr;
	if (!NetWorld || !Net)
	{
		return FReply::Unhandled();
	}
	const uint32 Self = Net->GetPlayer().Id;
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Ui->LookAt(Self);
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && NetWorld->IsChoosingTarget())
	{
		NetWorld->ChooseTarget(Self);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRUnitFrame

void SMRUnitFrame::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	auto Online = [this]() { return NetWorldOf(UI.Get()) ? EVisibility::Visible : EVisibility::Collapsed; };
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SMRPortrait, InUI)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f * Px, 4.f * Px, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					HudLabel(S, TAttribute<FText>::CreateSP(this, &SMRUnitFrame::Name), S->TitleFont(S->HudNumber(TEXT("name_size"), 20.f)),
						S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f * Px, 0.f, 0.f)
				[
					SNew(SHorizontalBox).Visibility_Lambda(Online)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f * Px, 0.f)
					[
						// command.c CommandRest / CommandStand: the button stays in while resting
						SNew(SMRToolButton, InUI).Piece(TEXT("btn_rest")).ToolTip(LOCTEXT("RestStand", "Rest / Stand (R)"))
							.bPressed_Lambda([this]() { const UMRNetWorldSubsystem* W = NetWorldOf(UI.Get()); return W && W->IsResting(); })
							.OnClicked_Lambda([this]()
							{
								if (UMRNetWorldSubsystem* W = NetWorldOf(UI.Get()))
								{
									W->SetResting(!W->IsResting());
								}
							})
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMRToolButton, InUI).Piece(TEXT("btn_mailbox")).ToolTip(LOCTEXT("Mail", "Read mail (L)"))
							.OnClicked_Lambda([this]()
							{
								if (UI.IsValid())
								{
									UI->SetWindowOpen(EMRWindow::Mail, true);
								}
							})
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 6.f * Px, 0.f, 0.f)
		[
			SNew(SMREnchantments, InUI).bHud(true).IconPx(S->HudNumber(TEXT("enchant_px"), 26.f)).WrapWidth(300.f)
		]
	];
}

FText SMRUnitFrame::Name() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRNetSubsystem* Net = Ui ? Ui->GetNet() : nullptr;
	if (NetWorldOf(Ui) && Net && Net->GetSelf())
	{
		return FText::FromString(Net->GetSelf()->Name);
	}
	return NetWorldOf(Ui) ? FText::GetEmpty() : LOCTEXT("Offline", "Adventurer");
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

// ------------------------------------------------------------------------------ SMRMapCluster

void SMRMapCluster::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->HudPx();
	const float Side = S->HudNumber(TEXT("map_side"), 172.f);
	const float Button = S->HudNumber(TEXT("map_button"), 26.f);
	const FLinearColor Gold = S->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	TWeakObjectPtr<UMRUISubsystem> WeakUI(InUI);
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
					// a click on the map opens the large map (and M); the large map closes itself
					SNew(SMRMinimap, InUI).bRound(true).Side(Side)
						.OnClicked_Lambda([WeakUI](FVector2D) { if (WeakUI.IsValid()) WeakUI->ToggleWindow(EMRWindow::Map); })
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
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f * Px, 0.f, 0.f)
			[
				HudLabel(S, TAttribute<FText>::CreateSP(this, &SMRMapCluster::RoomName), S->TitleFont(S->HudNumber(TEXT("room_name_size"), 16.f)), Gold)
			]
			// ours: the Meridian time, as the old map showed it
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				HudLabel(S, TAttribute<FText>::CreateSP(this, &SMRMapCluster::Clock), S->HudFont(S->HudNumber(TEXT("time_size"), 12.f)),
					S->HudColor(TEXT("text"), FLinearColor(0.8f, 0.8f, 0.8f)))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f * Px, 0.f, 0.f)
			[
				SNew(SMREnchantments, InUI).bRoom(true).bHud(true).IconPx(S->HudNumber(TEXT("enchant_px"), 26.f))
					.WrapWidth(S->HudNumber(TEXT("map_width"), 184.f)).bCentre(true)
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

FText SMRMapCluster::Clock() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	const UMRGameTimeSubsystem* Time = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRGameTimeSubsystem>() : nullptr;
	if (!Time)
	{
		return FText::GetEmpty();
	}
	const double H = Time->GetGameHour();
	const int32 Hour = FMath::FloorToInt(H) % 24;
	const int32 Min = FMath::FloorToInt(FMath::Frac(H) * 60.0);
	return FText::FromString(FString::Printf(TEXT("%d:%02d %s"), Hour % 12 == 0 ? 12 : Hour % 12, Min, Hour < 12 ? TEXT("AM") : TEXT("PM")));
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
	const float BarText = S->HudNumber(TEXT("bar_text_size"), 11.f);

	auto Row = [&](EMRSlotArea Area, bool bNumPad)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 i = 0; i < 9; ++i)
		{
			Box->AddSlot().AutoWidth().Padding(i > 0 ? SlotGap : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMRSlot, InUI, FMRSlotRef(Area, i)).bHud(true).Size(SlotPx)
					.KeyLabel(bNumPad ? FString::Printf(TEXT("N%d"), i + 1) : FString::FromInt(i + 1)).bSelectable(!bNumPad)
			];
		}
		return Box;
	};
	TSharedRef<SWidget> SpellRow = Row(EMRSlotArea::SpellBar, true);
	TSharedRef<SWidget> Items = Row(EMRSlotArea::Hotbar, false);
	const bool bSpellsFirst = S->HudNumber(TEXT("spell_row_first"), 1.f) > 0.f;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		// health and mana side by side, vigor under them (Shards' .hud-bar-row)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Gap * 0.5f, 0.f)
			[
				SNew(SMRHudBar, InUI).Vital(0).Kind(TEXT("health")).Height(S->HudNumber(TEXT("bar_h"), 18.f)).TextSize(BarText)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(Gap * 0.5f, 0.f, 0.f, 0.f)
			[
				SNew(SMRHudBar, InUI).Vital(1).Kind(TEXT("mana")).Height(S->HudNumber(TEXT("bar_h"), 18.f)).TextSize(BarText)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Gap, 0.f, 0.f)
		[
			SNew(SMRHudBar, InUI).Vital(2).Kind(TEXT("vigor")).Height(S->HudNumber(TEXT("vigor_h"), 12.f))
				.TextSize(S->HudNumber(TEXT("vigor_text_size"), 9.f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Gap, 0.f, 0.f)
		[
			bSpellsFirst ? SpellRow : Items
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Gap, 0.f, 0.f)
		[
			bSpellsFirst ? Items : SpellRow
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Gap, 0.f, 0.f)
		[
			SNew(SMRHudBar, InUI).Vital(3).Kind(TEXT("xp")).Height(S->HudNumber(TEXT("xp_h"), 6.f)).bText(false)
		];
	ChildSlot.Padding(Pad)[Box];
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
