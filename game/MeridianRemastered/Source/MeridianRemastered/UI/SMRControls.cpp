#include "UI/SMRControls.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

// ------------------------------------------------------------------------------ SMRSlider

void SMRSlider::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	Value = InArgs._Value;
	Min = InArgs._Min;
	Max = InArgs._Max;
	Width = InArgs._Width;
	Height = InArgs._Height;
	bShowValue = InArgs._bShowValue;
	Color = InArgs._Color;
	Ticks = InArgs._Ticks;
	bTrack = InArgs._bTrack;
	OnChanged = InArgs._OnChanged;
}

FVector2D SMRSlider::ComputeDesiredSize(float) const
{
	const float Px = UI.IsValid() && UI->GetStyle() ? UI->GetStyle()->Px() : 2.f;
	return FVector2D(Width * Px, (Height + 3.f) * Px);  // (the marker under the bar)
}

int32 SMRSlider::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUIStyle* Style = UI.IsValid() ? UI->GetStyle() : nullptr;
	if (!Style)
	{
		return Layer;
	}
	const float Px = Style->Px();
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint() * (bParentEnabled && IsEnabled() ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f));
	if (bTrack)
	{
		return PaintTrack(Geo, Out, Layer, Tint);
	}
	const FVector2f Size(Geo.GetLocalSize().X, Height * Px);
	const float F = FMath::Clamp(static_cast<float>(Value.Get() - Min) / (GetMax() - Min), 0.f, 1.f);
	const FSlateBrush* W = Style->White();
	// the original's colours (clientd3d/color.c): COLOR_BAR3 behind, COLOR_BAR1 the fill; a highlight along the top
	const FLinearColor Fill = Color.IsSet() ? Color.GetValue() : Style->Color(TEXT("graph_bar"), FLinearColor(0.f, 0.216f, 0.f));
	MRPaint::Box(Out, Layer, Geo, W, FVector2f::ZeroVector, Size, Tint * Style->Color(TEXT("graph_empty"), FLinearColor(0.03f, 0.f, 0.f)));
	MRPaint::Box(Out, Layer + 1, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * F, Size.Y), Tint * Fill);
	MRPaint::Box(Out, Layer + 2, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * F, Size.Y * 0.3f), Tint * FLinearColor(1.f, 1.f, 1.f, 0.15f));
	// the gold frame, as SMRBar
	const FVector2f CapL = Style->PieceSize(TEXT("bar_left")), CapR = Style->PieceSize(TEXT("bar_right"));
	if (CapL.Y > 0.f && CapR.Y > 0.f)
	{
		const float K = Size.Y / CapL.Y * 1.25f;
		const FVector2f L(CapL.X * K, Size.Y * 1.25f), R(CapR.X * K, Size.Y * 1.25f);
		const float Y = -Size.Y * 0.125f;
		const float Edge = Style->PieceSize(TEXT("bar_top")).Y;
		MRPaint::Tile(Out, Layer + 3, Geo, Style->Brush(TEXT("bar_top"), true), FVector2f(0.f, Y), FVector2f(Size.X, Edge), Tint);
		MRPaint::Tile(Out, Layer + 3, Geo, Style->Brush(TEXT("bar_bottom"), true), FVector2f(0.f, Size.Y - Y - Edge), FVector2f(Size.X, Edge), Tint);
		MRPaint::Box(Out, Layer + 4, Geo, Style->Brush(TEXT("bar_left")), FVector2f(-L.X * 0.6f, Y), L, Tint);
		MRPaint::Box(Out, Layer + 4, Geo, Style->Brush(TEXT("bar_right")), FVector2f(Size.X - R.X * 0.4f, Y), R, Tint);
	}
	// ticks (the skin slider's steps) and the marker under the value
	const int32 NumTicks = Ticks.Get();
	for (int32 i = 0; NumTicks > 1 && i < NumTicks; ++i)
	{
		const float X = Size.X * i / (NumTicks - 1);
		MRPaint::Box(Out, Layer + 5, Geo, W, FVector2f(X - 0.25f * Px, Size.Y + 0.5f * Px), FVector2f(0.5f * Px, 1.5f * Px), Tint * FLinearColor(0.85f, 0.8f, 0.7f));
	}
	const float MX = Size.X * F;
	MRPaint::Box(Out, Layer + 6, Geo, W, FVector2f(MX - 1.f * Px, Size.Y + 0.5f * Px), FVector2f(2.f * Px, 2.f * Px),
		Tint * Style->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)));
	if (bShowValue)
	{
		const FString Text = FString::FromInt(Value.Get());
		const FSlateFontInfo Font = Style->Font(FMath::Max(6.f, Height * 0.8f), true);
		const FVector2f M = MRPaint::MeasureText(Text, Font);
		const float TX = FMath::Clamp(MX + 1.f * Px, 0.f, Size.X - M.X - 1.f * Px);
		MRPaint::Text(Out, Layer + 7, Geo, Text, Font, FVector2f(TX, (Size.Y - M.Y) * 0.5f), FLinearColor::White * Tint, Px * 0.5f);
	}
	return Layer + 8;
}

int32 SMRSlider::PaintTrack(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FLinearColor& Tint) const
{
	// the original creator's skin slider (a trackbar): a line, a tick per step, a thumb on the value
	UMRUIStyle* Style = UI->GetStyle();
	const float Px = Style->Px();
	const FSlateBrush* W = Style->White();
	const FLinearColor Gold = Style->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f);
	const FVector2f Size(Geo.GetLocalSize());
	const float ThumbW = 3.f * Px;
	const float X0 = ThumbW * 0.5f, X1 = Size.X - ThumbW * 0.5f;
	const float LineY = Height * Px * 0.5f;
	const float LineH = 0.75f * Px;
	MRPaint::Box(Out, Layer, Geo, W, FVector2f(X0, LineY - LineH * 0.5f + 0.5f * Px), FVector2f(X1 - X0, LineH), Tint * Shadow);
	MRPaint::Box(Out, Layer + 1, Geo, W, FVector2f(X0, LineY - LineH * 0.5f), FVector2f(X1 - X0, LineH), Tint * Gold);
	const int32 NumTicks = Ticks.Get();
	for (int32 i = 0; NumTicks > 1 && i < NumTicks; ++i)
	{
		const float X = X0 + (X1 - X0) * i / (NumTicks - 1);
		MRPaint::Box(Out, Layer + 1, Geo, W, FVector2f(X - 0.5f * Px, LineY + 1.5f * Px), FVector2f(1.f * Px, 3.f * Px), Tint * Gold);
	}
	const float F = FMath::Clamp(static_cast<float>(Value.Get() - Min) / (GetMax() - Min), 0.f, 1.f);
	const float TX = X0 + (X1 - X0) * F;
	const FVector2f ThumbPos(TX - ThumbW * 0.5f, LineY - Height * Px * 0.45f);
	const FVector2f ThumbSize(ThumbW, Height * Px * 0.9f);
	MRPaint::Box(Out, Layer + 2, Geo, W, ThumbPos + FVector2f(0.5f, 0.5f) * Px, ThumbSize, Tint * Shadow);
	MRPaint::Box(Out, Layer + 3, Geo, W, ThumbPos, ThumbSize, Tint * Gold);
	MRPaint::Box(Out, Layer + 4, Geo, W, ThumbPos + FVector2f(0.5f, 0.5f) * Px, FVector2f(ThumbW - 1.f * Px, 1.f * Px), Tint * FLinearColor(1.f, 1.f, 1.f, 0.5f));
	return Layer + 5;
}

void SMRSlider::SetFromMouse(const FGeometry& Geo, const FPointerEvent& Event)
{
	const float X = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
	const float F = FMath::Clamp(X / FMath::Max(1.f, static_cast<float>(Geo.GetLocalSize().X)), 0.f, 1.f);
	const int32 V = Min + FMath::RoundToInt(F * (GetMax() - Min));
	if (V != Value.Get())
	{
		OnChanged.ExecuteIfBound(V);
	}
}

FReply SMRSlider::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	SetFromMouse(Geo, Event);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMRSlider::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event)
{
	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

FReply SMRSlider::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	SetFromMouse(Geo, Event);
	return FReply::Handled();
}

FCursorReply SMRSlider::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight) : FCursorReply::Unhandled();
}

// ------------------------------------------------------------------------------ SMRTextBox

void SMRTextBox::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	MaxLength = InArgs._MaxLength;
	OnChanged = InArgs._OnChanged;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	TextStyle = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	TextStyle.SetFont(S->Font(9.f)).SetColorAndOpacity(S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)));
	ChildSlot
	[
		SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(2.f)
		[
			SNew(SBox).WidthOverride(InArgs._Width * Px).HeightOverride(InArgs._Height * Px).Padding(2.f * Px, 1.f * Px)
			[
				// (the edit fills the box, so a click anywhere in it lands in the text)
				SAssignNew(Edit, SMultiLineEditableText)
				.Text(InArgs._InitialText)
				.HintText(InArgs._HintText)
				.TextStyle(&TextStyle)
				.AutoWrapText(true)
				.AllowMultiLine(true)
				.OnTextChanged_Lambda([this](const FText& T)
				{
					if (T.ToString().Len() > MaxLength)
					{
						Edit->SetText(FText::FromString(T.ToString().Left(MaxLength)));
					}
					OnChanged.ExecuteIfBound();
				})
			]
		]
	];
}

FString SMRTextBox::GetText() const
{
	return Edit.IsValid() ? Edit->GetText().ToString() : FString();
}

void SMRTextBox::SetText(const FString& InText)
{
	if (Edit.IsValid())
	{
		Edit->SetText(FText::FromString(InText));
	}
}

FReply SMRTextBox::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Focus();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SMRTextBox::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(EMouseCursor::TextEditBeam);
}

void SMRTextBox::Focus()
{
	if (Edit.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Edit, EFocusCause::SetDirectly);
	}
}

// ------------------------------------------------------------------------------ SMRSelectList

namespace
{
	/** One row of an SMRSelectList. */
	class SMRSelectRow : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMRSelectRow) {}
			SLATE_ARGUMENT(FText, Text)
			SLATE_ARGUMENT(float, TextSize)
			SLATE_ARGUMENT(bool, bRowEnabled)
			SLATE_ATTRIBUTE(bool, bSelected)
			SLATE_EVENT(FSimpleDelegate, OnClicked)
			SLATE_EVENT(FSimpleDelegate, OnDoubleClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
		{
			UI = InUI;
			Text = InArgs._Text.ToString();
			TextSize = InArgs._TextSize;
			bRowEnabled = InArgs._bRowEnabled;
			bSelected = InArgs._bSelected;
			OnClicked = InArgs._OnClicked;
			OnDoubleClicked = InArgs._OnDoubleClicked;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			return S ? FVector2D(MRPaint::MeasureText(Text, S->Font(TextSize)) + FVector2f(4.f, 1.f) * S->Px()) : FVector2D(100.f, 20.f);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
			if (!S)
			{
				return Layer;
			}
			const FVector2f Size(Geo.GetLocalSize());
			const bool bSel = bSelected.Get(false);
			if (bSel)
			{
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, Size, FLinearColor(0.93f, 0.9f, 0.82f));
			}
			else if (IsHovered())
			{
				MRPaint::Box(Out, Layer, Geo, S->White(), FVector2f::ZeroVector, Size, FLinearColor(1.f, 1.f, 1.f, 0.1f));
			}
			const FLinearColor C = bSel ? FLinearColor(0.02f, 0.02f, 0.02f) : (bRowEnabled ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f));
			// (clipped to the row: long names end under the scroll bar)
			MRPaint::Text(Out, Layer + 1, Geo, Text, S->Font(TextSize), FVector2f(2.f * S->Px(), 0.5f * S->Px()), C, bSel ? 0.f : 1.f);
			return Layer + 2;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			OnClicked.ExecuteIfBound();
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geo, const FPointerEvent& Event) override
		{
			OnDoubleClicked.ExecuteIfBound();
			return FReply::Handled();
		}

	private:
		TWeakObjectPtr<UMRUISubsystem> UI;
		FString Text;
		float TextSize = 9.f;
		bool bRowEnabled = true;
		TAttribute<bool> bSelected;
		FSimpleDelegate OnClicked;
		FSimpleDelegate OnDoubleClicked;
	};
}

void SMRSelectList::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	TextSize = InArgs._TextSize;
	OnSelected = InArgs._OnSelected;
	OnActivated = InArgs._OnActivated;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	ChildSlot
	[
		SNew(SMRPanel, InUI).Background(NAME_None).Frame(TEXT("inset")).Padding(1.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(S->White()).ColorAndOpacity(FLinearColor::Black)]
			+ SOverlay::Slot()
			[
				SNew(SBox).WidthOverride(InArgs._Width * Px).HeightOverride(InArgs._Height * Px)
				[
					SNew(SScrollBox).ScrollBarThickness(FVector2D(3.f * Px, 3.f * Px))
					+ SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]
				]
			]
		]
	];
}

void SMRSelectList::SetItems(const TArray<FText>& InItems, const TArray<bool>& InEnabled)
{
	Items = InItems;
	Enabled = InEnabled;
	Selected = Items.Num() > 0 ? FMath::Clamp(Selected, -1, Items.Num() - 1) : -1;
	Rebuild();
}

void SMRSelectList::SetSelected(int32 Index)
{
	Selected = Items.IsValidIndex(Index) ? Index : -1;
}

void SMRSelectList::Rebuild()
{
	if (!Rows.IsValid() || !UI.IsValid())
	{
		return;
	}
	Rows->ClearChildren();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Rows->AddSlot().AutoHeight()
		[
			SNew(SMRSelectRow, UI.Get()).Text(Items[i]).TextSize(TextSize)
				.bRowEnabled(!Enabled.IsValidIndex(i) || Enabled[i])
				.bSelected_Lambda([this, i]() { return Selected == i; })
				.OnClicked_Lambda([this, i]()
				{
					const bool bAgain = Selected == i;
					Selected = i;
					OnSelected.ExecuteIfBound(i);
					if (bAgain)
					{
						OnActivated.ExecuteIfBound(i);
					}
				})
				.OnDoubleClicked_Lambda([this, i]() { OnActivated.ExecuteIfBound(i); })
		];
	}
}
