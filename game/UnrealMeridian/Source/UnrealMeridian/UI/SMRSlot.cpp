#include "UI/SMRSlot.h"

#include "Rendering/DrawElements.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "Widgets/IToolTip.h"

void SMRSlot::Construct(const FArguments& InArgs, UMRUISubsystem* InUI, const FMRSlotRef& InSlot)
{
	UI = InUI;
	SlotRef = InSlot;
	SizePx = InArgs._Size;
	KeyLabel = InArgs._KeyLabel;
	bSelectable = InArgs._bSelectable;
	bToolTip = InArgs._bToolTip;
	HintIcon = InArgs._HintIcon;
	bHud = InArgs._bHud;
}

FVector2D SMRSlot::ComputeDesiredSize(float) const
{
	const UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float S = SizePx * (Style ? (bHud ? Style->HudPx() : Style->Px()) : 2.f);
	return FVector2D(S, S);
}

int32 SMRSlot::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui ? Ui->GetStyle() : nullptr;
	UMRInventorySource* Source = Ui ? Ui->GetSource() : nullptr;
	if (!Style || !Source)
	{
		return Layer;
	}
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const float Px = Style->Px();
	Ui->NoteSlotDrawn(SlotRef, FVector2f(Geo.LocalToAbsolute(FVector2D(Size * 0.5f))));
	if (bHud)
	{
		return PaintHud(Geo, Out, Layer, WStyle);
	}

	// the sunk stone square
	MRPaint::Box(Out, Layer, Geo, Style->Brush(TEXT("slot")), FVector2f::ZeroVector, Size, Tint);

	const FMRSlotContent C = Source->Get(SlotRef);
	const float Pad = Size.X * 0.1f;
	const FVector2f IconPos(Pad, Pad), IconSize(Size.X - 2.f * Pad, Size.Y - 2.f * Pad);
	if (!C.IsEmpty())
	{
		MRPaint::Box(Out, Layer + 1, Geo, Ui->IconFor(C), IconPos, IconSize, Tint);
	}
	else if (!HintIcon.IsNone())
	{
		MRPaint::Box(Out, Layer + 1, Geo, Style->Icon(HintIcon), IconPos, IconSize,
			Tint * FLinearColor(0.f, 0.f, 0.f, Style->Number(TEXT("slot_hint_opacity"), 0.35f)));
	}

	// cooldown after a cast: a dark shade that drains upward
	if (SlotRef.Area == EMRSlotArea::SpellBar)
	{
		const float Cd = Ui->SpellCooldown(SlotRef.Index);
		if (Cd > 0.f)
		{
			MRPaint::Box(Out, Layer + 2, Geo, Style->White(), FVector2f(0.f, Size.Y * (1.f - Cd)), FVector2f(Size.X, Size.Y * Cd),
				Tint * FLinearColor(0.f, 0.f, 0.f, 0.55f));
		}
	}

	// hover
	if (IsHovered())
	{
		MRPaint::Box(Out, Layer + 2, Geo, Style->White(), FVector2f(Px, Px), Size - FVector2f(2.f * Px, 2.f * Px),
			Tint * FLinearColor(1.f, 1.f, 1.f, 0.18f));
	}

	// count (stacks) and key label
	const FLinearColor TextColor = Style->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)) * Tint;
	if (!C.IsEmpty() && C.Count > 1)
	{
		const FString Count = FString::FromInt(C.Count);
		const FSlateFontInfo Font = Style->Font(9.f, true);
		const FVector2f M = MRPaint::MeasureText(Count, Font);
		MRPaint::Text(Out, Layer + 3, Geo, Count, Font, Size - M - FVector2f(Px, 0.f), FLinearColor::White * Tint, Px * 0.5f);
	}
	if (!KeyLabel.IsEmpty())
	{
		MRPaint::Text(Out, Layer + 3, Geo, KeyLabel, Style->Font(7.f), FVector2f(Px * 1.5f, Px * 0.5f),
			TextColor * FLinearColor(1.f, 1.f, 1.f, 0.8f), Px * 0.5f);
	}

	// the selected hotbar slot: the gold frame, a little larger than the slot
	if (bSelectable && SlotRef.Area == EMRSlotArea::Hotbar && Source->GetSelectedHotbar() == SlotRef.Index)
	{
		const float Grow = Px * 1.5f;
		MRPaint::Box(Out, Layer + 5, Geo, Style->Brush(TEXT("slot_selected")), FVector2f(-Grow, -Grow), Size + FVector2f(2.f * Grow, 2.f * Grow), Tint);
	}
	return Layer + 6;
}

int32 SMRSlot::PaintHud(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& WStyle) const
{
	// Shards' quick slot (styles.css .game.modern-ui .hotbar-slot): a box sunk into the stone, a
	// black edge that turns gold on the selected slot or under the mouse, the key top left and the
	// count bottom right
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* Style = Ui->GetStyle();
	UMRInventorySource* Source = Ui->GetSource();
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const FVector2f Size(Geo.GetLocalSize());
	const float Px = Style->HudPx();
	const float Radius = 2.f * Px;
	MRPaint::Rounded(Out, Layer, Geo, FVector2f::ZeroVector, Size, Style->HudColor(TEXT("slot_bg"), FLinearColor(0.011f, 0.011f, 0.011f)) * Tint, Radius);
	MRPaint::Tile(Out, Layer + 1, Geo, Style->Brush(TEXT("invbkgnd"), true), FVector2f(Px, Px), Size - FVector2f(2.f * Px, 2.f * Px), Tint);
	// the inset shadow (inset 2px 2px 4px black 0.8; inset -1px -1px 0 white 0.08)
	const FLinearColor Dark = FLinearColor(0.f, 0.f, 0.f, 0.8f) * Tint;
	const float Band = 4.f * Px;
	FSlateDrawElement::MakeGradient(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Size.X, Band), FSlateLayoutTransform(FVector2f::ZeroVector)),
		{FSlateGradientStop(FVector2f(0.f, 0.f), Dark), FSlateGradientStop(FVector2f(0.f, Band), FLinearColor::Transparent)}, Orient_Horizontal);
	FSlateDrawElement::MakeGradient(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Band, Size.Y), FSlateLayoutTransform(FVector2f::ZeroVector)),
		{FSlateGradientStop(FVector2f(0.f, 0.f), Dark), FSlateGradientStop(FVector2f(Band, 0.f), FLinearColor::Transparent)}, Orient_Vertical);
	const FLinearColor Lit = FLinearColor(1.f, 1.f, 1.f, 0.08f) * Tint;
	MRPaint::Box(Out, Layer + 2, Geo, Style->White(), FVector2f(Px, Size.Y - 2.f * Px), FVector2f(Size.X - 2.f * Px, Px), Lit);
	MRPaint::Box(Out, Layer + 2, Geo, Style->White(), FVector2f(Size.X - 2.f * Px, Px), FVector2f(Px, Size.Y - 3.f * Px), Lit);

	const FMRSlotContent C = Source->Get(SlotRef);
	const float Pad = 4.f * Px;  // its 3 px padding inside the 1 px edge
	if (!C.IsEmpty())
	{
		MRPaint::Box(Out, Layer + 3, Geo, Ui->IconFor(C), FVector2f(Pad, Pad), Size - FVector2f(2.f * Pad, 2.f * Pad), Tint);
	}
	if (SlotRef.Area == EMRSlotArea::SpellBar)
	{
		const float Cd = Ui->SpellCooldown(SlotRef.Index);
		if (Cd > 0.f)
		{
			MRPaint::Box(Out, Layer + 4, Geo, Style->White(), FVector2f(Px, Px + (Size.Y - 2.f * Px) * (1.f - Cd)),
				FVector2f(Size.X - 2.f * Px, (Size.Y - 2.f * Px) * Cd), Tint * FLinearColor(0.f, 0.f, 0.f, 0.55f));
		}
	}
	const FLinearColor Gold = Style->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	const bool bSelected = bSelectable && SlotRef.Area == EMRSlotArea::Hotbar && Source->GetSelectedHotbar() == SlotRef.Index;
	MRPaint::Rounded(Out, Layer + 5, Geo, FVector2f::ZeroVector, Size, FLinearColor::Transparent, Radius,
		(bSelected || IsHovered() ? Gold : FLinearColor::Black) * Tint, Px);
	if (bSelected)
	{
		// Shards' in-use glow (inset 0 0 6px gold 0.6)
		MRPaint::Rounded(Out, Layer + 5, Geo, FVector2f(Px, Px), Size - FVector2f(2.f * Px, 2.f * Px), FLinearColor::Transparent, Radius,
			Gold * FLinearColor(1.f, 1.f, 1.f, 0.35f) * Tint, 3.f * Px);
	}
	const FSlateFontInfo Font = Style->HudFont(Style->HudNumber(TEXT("slot_text_size"), 10.f), true);
	const FLinearColor TextColor = Style->HudColor(TEXT("slot_text"), FLinearColor(0.89f, 0.76f, 0.46f)) * Tint;
	if (!KeyLabel.IsEmpty())
	{
		MRPaint::Text(Out, Layer + 6, Geo, KeyLabel, Font, FVector2f(3.f * Px, 1.f * Px), TextColor, Px);
	}
	if (!C.IsEmpty() && C.Count > 1)
	{
		const FString Count = FString::FromInt(C.Count);
		const FVector2f M = MRPaint::MeasureText(Count, Font);
		MRPaint::Text(Out, Layer + 6, Geo, Count, Font, Size - M - FVector2f(3.f * Px, 1.f * Px), TextColor, Px);
	}
	return Layer + 8;
}

FReply SMRSlot::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	const bool bLeft = Event.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bRight = Event.GetEffectingButton() == EKeys::RightMouseButton;
	if (UI.IsValid() && bHud && bLeft && !UI->IsInventoryOpen())
	{
		// the action bar with the dialog closed: a click uses the slot (an item is put on, a spell cast)
		if (SlotRef.Area == EMRSlotArea::Hotbar)
		{
			UI->UseHotbarSlot(SlotRef.Index);
			return FReply::Handled();
		}
		if (SlotRef.Area == EMRSlotArea::SpellBar)
		{
			UI->OnSpellKey(SlotRef.Index);
			return FReply::Handled();
		}
	}
	if (!UI.IsValid() || !(bLeft || bRight) || !UI->IsInventoryOpen())
	{
		return FReply::Unhandled();
	}
	UI->NoteMouse(Event.GetScreenSpacePosition());
	UI->OnSlotMouseDown(SlotRef, bRight, Event.IsShiftDown());
	return FReply::Handled();
}

FReply SMRSlot::OnMouseButtonDoubleClick(const FGeometry& Geo, const FPointerEvent& Event)
{
	// a double click is two clicks (Minecraft's collect-all isn't implemented)
	return OnMouseButtonDown(Geo, Event);
}

FReply SMRSlot::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!UI.IsValid() || Event.GetEffectingButton() != EKeys::LeftMouseButton || !UI->IsInventoryOpen())
	{
		return FReply::Unhandled();
	}
	UI->OnSlotMouseUp(SlotRef);
	return FReply::Handled();
}

FReply SMRSlot::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (UI.IsValid())
	{
		UI->NoteMouse(Event.GetScreenSpacePosition());  // for the carried stack (the spell bar is outside the dialog)
	}
	return FReply::Unhandled();
}

void SMRSlot::OnMouseEnter(const FGeometry& Geo, const FPointerEvent& Event)
{
	SLeafWidget::OnMouseEnter(Geo, Event);
	if (UI.IsValid())
	{
		UI->SetHoveredSlot(SlotRef, true);
	}
}

void SMRSlot::OnMouseLeave(const FPointerEvent& Event)
{
	SLeafWidget::OnMouseLeave(Event);
	if (UI.IsValid())
	{
		UI->SetHoveredSlot(SlotRef, false);
	}
}

TSharedPtr<IToolTip> SMRSlot::GetToolTip()
{
	UMRUISubsystem* Ui = UI.Get();
	if (!bToolTip || !Ui || !Ui->GetSource() || !Ui->IsInventoryOpen())
	{
		return nullptr;
	}
	// nothing while carrying a stack (as Minecraft), and only for a filled slot
	if (!Ui->GetSource()->Get(FMRSlotRef(EMRSlotArea::Cursor, 0)).IsEmpty())
	{
		return nullptr;
	}
	const FMRSlotContent C = Ui->GetSource()->Get(SlotRef);
	if (C.IsEmpty())
	{
		return nullptr;
	}
	if (ToolTipFor != C.Id || !CachedToolTip.IsValid())
	{
		ToolTipFor = C.Id;
		CachedToolTip = Ui->MakeToolTip(C);
	}
	return CachedToolTip;
}
