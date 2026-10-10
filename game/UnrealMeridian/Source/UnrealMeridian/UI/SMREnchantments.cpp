#include "UI/SMREnchantments.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetSubsystem.h"
#include "Rendering/DrawElements.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"

namespace
{
	constexpr float GapPx = 2.f;
	constexpr float HudGapPx = 3.f;
}

void SMREnchantments::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	bRoom = InArgs._bRoom;
	IconPx = InArgs._IconPx;
	bHud = InArgs._bHud;
	WrapWidth = InArgs._WrapWidth;
	bCentre = InArgs._bCentre;
}

const TArray<FMRNetObject>* SMREnchantments::List() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
	const UMRNetSubsystem* Net = GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame)
	{
		return nullptr;
	}
	return bRoom ? &Net->GetRoomEnchantments() : &Net->GetPlayerEnchantments();
}

float SMREnchantments::Unit() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	return S ? (bHud ? S->HudPx() : S->Px()) : 1.f;
}

float SMREnchantments::Gap() const
{
	return bHud ? HudGapPx : GapPx;
}

int32 SMREnchantments::PerRow(int32 N) const
{
	if (WrapWidth <= 0.f)
	{
		return FMath::Max(1, N);
	}
	return FMath::Max(1, FMath::FloorToInt32((WrapWidth + Gap()) / (IconPx + Gap())));
}

FVector2f SMREnchantments::IconAt(int32 i, int32 N, float RowWidth) const
{
	const int32 Per = PerRow(N);
	const int32 Row = i / Per, Col = i % Per;
	const int32 InRow = FMath::Min(Per, N - Row * Per);
	const float Step = (IconPx + Gap()) * Unit();
	const float Width = (InRow * (IconPx + Gap()) - Gap()) * Unit();
	const float X0 = bCentre ? (RowWidth - Width) * 0.5f : 0.f;
	return FVector2f(X0 + Col * Step, Row * Step);
}

FVector2D SMREnchantments::ComputeDesiredSize(float) const
{
	const TArray<FMRNetObject>* L = List();
	const int32 N = L ? L->Num() : 0;
	if (N == 0)
	{
		return FVector2D::ZeroVector;
	}
	const int32 Per = PerRow(N);
	const int32 Cols = FMath::Min(Per, N), Rows = (N + Per - 1) / Per;
	const float U = Unit();
	return FVector2D((Cols * (IconPx + Gap()) - Gap()) * U, (Rows * (IconPx + Gap()) - Gap()) * U);
}

int32 SMREnchantments::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	const TArray<FMRNetObject>* L = List();
	if (!S || !L)
	{
		return Layer;
	}
	const float U = Unit();
	const float Size = IconPx * U;
	const float RowWidth = Geo.GetLocalSize().X;
	for (int32 i = 0; i < L->Num(); ++i)
	{
		const FVector2f At = IconAt(i, L->Num(), RowWidth);
		FVector2f IconPos = At, IconSize(Size, Size);
		if (bHud)
		{
			// Shards: 26 px, 1 px padding, the HUD panel with its gold edge, rounded 3 px
			MRPaint::Rounded(Out, Layer, Geo, At, FVector2f(Size, Size), S->HudColor(TEXT("panel"), FLinearColor(0.005f, 0.004f, 0.003f, 0.66f)),
				3.f * U, S->HudColor(TEXT("edge"), FLinearColor(0.67f, 0.46f, 0.1f, 0.55f)), U);
			IconPos += FVector2f(2.f * U, 2.f * U);
			IconSize -= FVector2f(4.f * U, 4.f * U);
		}
		else
		{
			// a dark square behind each, as the original's buttons
			FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(At)), S->White(),
				ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.45f));
		}
		if (const FSlateBrush* Icon = Ui->EnchantmentIcon((*L)[i]))
		{
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(IconSize, FSlateLayoutTransform(IconPos)), Icon);
		}
	}
	return Layer + 2;
}

int32 SMREnchantments::IconUnder(const FGeometry& Geo, const FPointerEvent& Event) const
{
	const TArray<FMRNetObject>* L = List();
	const FVector2f Local = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const float Size = IconPx * Unit();
	for (int32 i = 0; L && i < L->Num(); ++i)
	{
		const FVector2f At = IconAt(i, L->Num(), Geo.GetLocalSize().X);
		if (Local.X >= At.X && Local.Y >= At.Y && Local.X < At.X + Size && Local.Y < At.Y + Size)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FReply SMREnchantments::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	// name the one under the mouse
	const TArray<FMRNetObject>* L = List();
	const int32 Found = IconUnder(Geo, Event);
	SetToolTipText(L && Found != INDEX_NONE ? FText::FromString((*L)[Found].Name) : FText::GetEmpty());
	return FReply::Unhandled();
}

FReply SMREnchantments::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	// a right click looks at one (enchant.c WM_RBUTTONDOWN)
	const TArray<FMRNetObject>* L = List();
	const int32 Found = IconUnder(Geo, Event);
	if (Event.GetEffectingButton() != EKeys::RightMouseButton || !L || Found == INDEX_NONE || !UI.IsValid())
	{
		return FReply::Unhandled();
	}
	UI->LookAt((*L)[Found].Id);
	return FReply::Handled();
}
