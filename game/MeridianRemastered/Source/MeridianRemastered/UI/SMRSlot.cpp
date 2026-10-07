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
	HintIcon = InArgs._HintIcon;
}

FVector2D SMRSlot::ComputeDesiredSize(float) const
{
	const UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float S = SizePx * (Style ? Style->Px() : 2.f);
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

FReply SMRSlot::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	const bool bLeft = Event.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bRight = Event.GetEffectingButton() == EKeys::RightMouseButton;
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
	if (!Ui || !Ui->GetSource() || !Ui->IsInventoryOpen())
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
