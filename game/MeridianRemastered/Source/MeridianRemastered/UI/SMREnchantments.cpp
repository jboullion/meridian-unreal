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
}

void SMREnchantments::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	bRoom = InArgs._bRoom;
	IconPx = InArgs._IconPx;
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

FVector2D SMREnchantments::ComputeDesiredSize(float) const
{
	const UMRUISubsystem* Ui = UI.Get();
	const TArray<FMRNetObject>* L = List();
	const float Px = Ui && Ui->GetStyle() ? Ui->GetStyle()->Px() : 1.f;
	const int32 N = L ? L->Num() : 0;
	return N == 0 ? FVector2D::ZeroVector : FVector2D((N * (IconPx + GapPx) - GapPx) * Px, IconPx * Px);
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
	const float Px = S->Px();
	const float Size = IconPx * Px;
	for (int32 i = 0; i < L->Num(); ++i)
	{
		const FVector2f At((IconPx + GapPx) * Px * i, 0.f);
		// a dark square behind each, as the original's buttons
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(At)), S->White(),
			ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.45f));
		if (const FSlateBrush* Icon = Ui->EnchantmentIcon((*L)[i]))
		{
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(At)), Icon);
		}
	}
	return Layer + 2;
}

FReply SMREnchantments::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event)
{
	// name the one under the mouse
	const UMRUISubsystem* Ui = UI.Get();
	const TArray<FMRNetObject>* L = List();
	const float Px = Ui && Ui->GetStyle() ? Ui->GetStyle()->Px() : 1.f;
	const float X = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
	const int32 Index = FMath::FloorToInt32(X / ((IconPx + GapPx) * Px));
	SetToolTipText(L && L->IsValidIndex(Index) ? FText::FromString((*L)[Index].Name) : FText::GetEmpty());
	return FReply::Unhandled();
}
