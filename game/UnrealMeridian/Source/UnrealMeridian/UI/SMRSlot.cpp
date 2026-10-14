#include "UI/SMRSlot.h"

#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UI/MRInventorySource.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "Widgets/IToolTip.h"

namespace
{
	/** A white texture for the cooldown's fan (Slate's custom vertices need a texture; the vertex colour tints it). */
	const FSlateBrush* WhiteTexture()
	{
		static FSlateBrush Brush;
		static bool bMade = false;
		if (!bMade)
		{
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")))
			{
				Tex->AddToRoot();
				UMRUIStyle::SetImage(Brush, Tex, FVector2f(8.f, 8.f));
			}
			bMade = true;
		}
		return Brush.GetResourceObject() ? &Brush : nullptr;
	}

	/**
	 * Shards' spell cooldown (styles.css .qs-cooldown, a conic gradient): a shadow over the slot that
	 * clears clockwise from the top as the time runs out. Left: 0..1 of the time still to go.
	 */
	void DrawSweep(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, FVector2f Pos, FVector2f Size, float Left, const FLinearColor& Color)
	{
		const FSlateBrush* Brush = WhiteTexture();
		if (!Brush || Left <= 0.f)
		{
			return;
		}
		const FSlateResourceHandle Handle = Brush->GetRenderingResource(FVector2f(Brush->ImageSize), 1.f);
		if (!Handle.IsValid())
		{
			return;
		}
		const FVector2f C = Pos + Size * 0.5f;
		const FVector2f Half = Size * 0.5f;
		// where a ray from the centre at an angle (0 at the top, clockwise) leaves the box
		auto Edge = [&](float Turn)
		{
			const float A = Turn * UE_TWO_PI;
			const FVector2f D(FMath::Sin(A), -FMath::Cos(A));
			const float T = FMath::Min(FMath::Abs(D.X) > 1e-4f ? Half.X / FMath::Abs(D.X) : 1e6f, FMath::Abs(D.Y) > 1e-4f ? Half.Y / FMath::Abs(D.Y) : 1e6f);
			return C + D * T;
		};
		const FSlateRenderTransform& Transform = Geo.GetAccumulatedRenderTransform();
		const FColor Col = Color.ToFColor(true);
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Add(FSlateVertex::Make(Transform, C, FVector2f(0.5f, 0.5f), Col));
		// the shadow: from where the done part ends round to the top again; corners land on the steps (every 1/8)
		const float Done = 1.f - FMath::Clamp(Left, 0.f, 1.f);
		TArray<float> Turns = {Done};
		for (float T = FMath::FloorToFloat(Done * 8.f + 1.f) / 8.f; T < 1.f; T += 1.f / 8.f)
		{
			Turns.Add(T);
		}
		Turns.Add(1.f);
		for (const float T : Turns)
		{
			Verts.Add(FSlateVertex::Make(Transform, Edge(T), FVector2f(0.5f, 0.5f), Col));
		}
		for (int32 i = 1; i + 1 < Verts.Num(); ++i)
		{
			Indices.Add(0);
			Indices.Add(static_cast<SlateIndex>(i));
			Indices.Add(static_cast<SlateIndex>(i + 1));
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
	}
}

void SMRSlot::Construct(const FArguments& InArgs, UMRUISubsystem* InUI, const FMRSlotRef& InSlot)
{
	UI = InUI;
	SlotRef = InSlot;
	SizePx = InArgs._Size;
	KeyLabel = InArgs._KeyLabel;
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
	return Layer + 6;
}

int32 SMRSlot::PaintHud(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& WStyle) const
{
	// Shards' quick slot (styles.css .game.modern-ui .hotbar-slot, QuickSlots.tsx SlotFace): a box sunk
	// into the stone, a black edge that turns gold under the mouse or while its item is in use, the
	// key top left and the count carried bottom right; greyed when its spell isn't known or its item
	// isn't carried
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

	const bool bQuick = SlotRef.Area == EMRSlotArea::QuickSlot;
	const FMRSlotContent C = Source->Get(SlotRef);
	const bool bAvailable = !bQuick || Source->IsQuickSlotAvailable(SlotRef.Index);
	const bool bInUse = bQuick && Source->IsQuickSlotInUse(SlotRef.Index);
	const float Pad = 4.f * Px;  // its 3 px padding inside the 1 px edge
	if (!C.IsEmpty())
	{
		const FSlateBrush* Icon = Ui->IconFor(C);
		if (!Icon && bQuick && !Source->GetQuickSlot(SlotRef.Index).Icon.IsEmpty())
		{
			Icon = Ui->BitmapIcon(Source->GetQuickSlot(SlotRef.Index).Icon, 1);  // an item none of is carried: its saved bitmap
		}
		// Shards' .qs-icon.unavailable: grey, at 40 %
		const FLinearColor IconTint = bAvailable ? Tint : Tint * FLinearColor(0.45f, 0.45f, 0.45f, 0.4f);
		MRPaint::Box(Out, Layer + 3, Geo, Icon, FVector2f(Pad, Pad), Size - FVector2f(2.f * Pad, 2.f * Pad), IconTint);
	}
	const FSlateFontInfo Font = Style->HudFont(Style->HudNumber(TEXT("slot_text_size"), 10.f), true);
	const FLinearColor TextColor = Style->HudColor(TEXT("slot_text"), FLinearColor(0.89f, 0.76f, 0.46f)) * Tint;
	if (bQuick && bAvailable && !C.IsEmpty() && C.bSpell)
	{
		const float Cd = Ui->SpellCooldown(SlotRef.Index);
		if (Cd > 0.f)
		{
			DrawSweep(Out, Layer + 4, Geo, FVector2f(Px, Px), Size - FVector2f(2.f * Px, 2.f * Px), Cd, FLinearColor(0.f, 0.f, 0.f, 0.68f) * Tint);
			const FString Secs = FString::FromInt(FMath::CeilToInt(Ui->SpellCooldownSeconds(SlotRef.Index)));
			const FSlateFontInfo Big = Style->HudFont(15.f, true);
			const FVector2f M = MRPaint::MeasureText(Secs, Big);
			MRPaint::Text(Out, Layer + 7, Geo, Secs, Big, (Size - M) * 0.5f, FLinearColor::White * Tint, Px);
		}
	}
	const FLinearColor Gold = Style->HudColor(TEXT("gold"), FLinearColor(0.87f, 0.63f, 0.01f));
	MRPaint::Rounded(Out, Layer + 5, Geo, FVector2f::ZeroVector, Size, FLinearColor::Transparent, Radius,
		(bInUse || IsHovered() ? Gold : FLinearColor::Black) * Tint, Px);
	if (bInUse)
	{
		// Shards' in-use glow (inset 0 0 6px gold 0.6)
		MRPaint::Rounded(Out, Layer + 5, Geo, FVector2f(Px, Px), Size - FVector2f(2.f * Px, 2.f * Px), FLinearColor::Transparent, Radius,
			Gold * FLinearColor(1.f, 1.f, 1.f, 0.35f) * Tint, 3.f * Px);
	}
	if (!KeyLabel.IsEmpty())
	{
		MRPaint::Text(Out, Layer + 6, Geo, KeyLabel, Font, FVector2f(3.f * Px, 1.f * Px), TextColor, Px);
	}
	if (!C.IsEmpty() && !C.bSpell && C.Count > 1)
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
	if (!UI.IsValid() || !(bLeft || bRight))
	{
		return FReply::Unhandled();
	}
	UI->NoteMouse(Event.GetScreenSpacePosition());
	if (SlotRef.Area == EMRSlotArea::QuickSlot)
	{
		// the dialog open or not: a click uses it (on release: a drag onto another slot swaps them),
		// a right click opens the picker, and a carried stack or spell is named here
		UI->OnQuickSlotMouseDown(SlotRef.Index, bRight, Event.IsShiftDown());
		return FReply::Handled();
	}
	if (!UI->IsInventoryOpen())
	{
		return FReply::Unhandled();
	}
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
	if (!UI.IsValid() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	if (SlotRef.Area == EMRSlotArea::QuickSlot)
	{
		UI->OnQuickSlotMouseUp(SlotRef.Index);
		return FReply::Handled();
	}
	if (!UI->IsInventoryOpen())
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
		UI->NoteMouse(Event.GetScreenSpacePosition());  // for the carried stack (the quick slots are outside the dialog)
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
	if (!bToolTip || !Ui || !Ui->GetSource())
	{
		return nullptr;
	}
	// nothing while carrying a stack (as Minecraft)
	if (!Ui->GetSource()->Get(FMRSlotRef(EMRSlotArea::Cursor, 0)).IsEmpty())
	{
		return nullptr;
	}
	if (SlotRef.Area == EMRSlotArea::QuickSlot)
	{
		// its name and key (Shards' slotTitle), made again when what it names or whether it's there changes
		UMRInventorySource* Source = Ui->GetSource();
		const FName Key(*(Source->GetQuickSlot(SlotRef.Index).ToString() + (Source->IsQuickSlotAvailable(SlotRef.Index) ? TEXT("+") : TEXT("-"))));
		if (ToolTipFor != Key || !CachedToolTip.IsValid())
		{
			ToolTipFor = Key;
			CachedToolTip = Ui->MakeQuickSlotToolTip(SlotRef.Index);
		}
		return CachedToolTip;
	}
	if (!Ui->IsInventoryOpen())
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
