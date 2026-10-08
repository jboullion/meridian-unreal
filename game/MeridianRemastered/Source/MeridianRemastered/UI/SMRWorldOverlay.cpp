#include "UI/SMRWorldOverlay.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "Rendering/DrawElements.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"

namespace
{
	FLinearColor NameColor(const FMRNetObject& O)
	{
		if (O.DrawEffect == MRMsg::DRAWFX_BLACK)
		{
			return FLinearColor::Black;
		}
		const FColor C((O.NameColor >> 16) & 0xFF, (O.NameColor >> 8) & 0xFF, O.NameColor & 0xFF, 255);
		return FLinearColor(C);
	}

	/** Corner brackets around a box (local units). */
	void Brackets(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FVector2f& Min, const FVector2f& Max,
		const FLinearColor& Color, float Len, float Thick)
	{
		const FVector2f Corners[4] = {Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y)};
		const FVector2f Dirs[4][2] = {{{1, 0}, {0, 1}}, {{-1, 0}, {0, 1}}, {{-1, 0}, {0, -1}}, {{1, 0}, {0, -1}}};
		for (int32 i = 0; i < 4; ++i)
		{
			for (int32 k = 0; k < 2; ++k)
			{
				TArray<FVector2f> Line = {Corners[i], Corners[i] + Dirs[i][k] * Len};
				FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Line, ESlateDrawEffect::None, Color, true, Thick);
			}
		}
	}
}

void SMRWorldOverlay::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SMRWorldOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UMRNetWorldSubsystem* NetWorld = World ? World->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const UMRNetSubsystem* Net = GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (!S || !NetWorld || !Net || !NetWorld->IsActive() || !PC->GetPawn())
	{
		return Layer;
	}
	const float Px = S->Px();
	// viewport pixels -> this widget (it covers the viewport)
	const float ToLocal = 1.f / FMath::Max(0.01f, Geo.Scale);
	const APawn* Me = PC->GetPawn();
	const uint32 Target = NetWorld->GetTargetId();
	const uint32 Aim = NetWorld->GetAimId();

	for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
	{
		const AMRNetObject* A = Pair.Value.Get();
		const FMRNetObject* O = Net->FindObject(Pair.Key);
		if (!A || !O || O->DrawEffect == MRMsg::DRAWFX_INVISIBLE)
		{
			continue;
		}
		const bool bTarget = Pair.Key == Target;
		const bool bAim = Pair.Key == Aim && !bTarget;
		const double Dist = FVector::Dist2D(A->GetActorLocation(), Me->GetActorLocation());
		const bool bSign = (O->Flags & MRMsg::OF_SIGN) != 0;
		const bool bName = (O->Flags & MRMsg::OF_DISPLAY_NAME) && (bSign || Dist < UMRNetWorldSubsystem::NameDistanceCm());
		if (!bName && !bTarget && !bAim)
		{
			continue;
		}
		FVector2D TopPx, FeetPx;
		const FVector Feet = A->GetActorLocation() - FVector(0.0, 0.0, 88.0);
		if (!PC->ProjectWorldLocationToScreen(A->GetNameAnchor(), TopPx, true) || !PC->ProjectWorldLocationToScreen(Feet, FeetPx, true))
		{
			continue;
		}
		// not through walls
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNamePlate), false, Me);
		Params.AddIgnoredActor(A);
		FHitResult Hit;
		const FVector Camera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Me->GetActorLocation();
		if (World->LineTraceSingleByChannel(Hit, Camera, A->GetNameAnchor(), ECC_WorldStatic, Params) && !(Hit.GetActor() && Hit.GetActor()->IsA<AMRNetObject>()))
		{
			continue;
		}
		const FVector2f Top = FVector2f(TopPx) * ToLocal, Bottom = FVector2f(FeetPx) * ToLocal;
		if (bName || bTarget)
		{
			// smaller with distance, as the original's name glyphs
			const float Size = FMath::Lerp(9.f, 6.5f, FMath::Clamp(static_cast<float>(Dist / UMRNetWorldSubsystem::NameDistanceCm()), 0.f, 1.f));
			const FSlateFontInfo Font = S->Font(Size, true);
			const FString& Name = O->Name;
			const FVector2f M = MRPaint::MeasureText(Name, Font);
			MRPaint::Text(Out, Layer + 1, Geo, Name, Font, Top - FVector2f(M.X * 0.5f, M.Y), NameColor(*O), Px * 0.5f);
		}
		if (bTarget || bAim)
		{
			const float H = FMath::Max(12.f * Px, Bottom.Y - Top.Y);
			const FVector2f Min(Top.X - H * 0.32f, Top.Y), Max(Top.X + H * 0.32f, Bottom.Y);
			const FLinearColor Color = bTarget ? ((O->Flags & MRMsg::OF_ATTACKABLE) ? FLinearColor(0.95f, 0.15f, 0.1f, 0.95f) : FLinearColor(1.f, 0.8f, 0.3f, 0.95f))
				: FLinearColor(1.f, 1.f, 1.f, 0.45f);
			Brackets(Out, Layer + 1, Geo, Min, Max, Color, FMath::Min(8.f * Px, H * 0.25f), bTarget ? 1.5f * Px : 1.f * Px);
		}
	}

	// the crosshair while the mouse looks around (the aim picks targets)
	if (!PC->bShowMouseCursor)
	{
		const FVector2f C = FVector2f(Geo.GetLocalSize()) * 0.5f;
		const float R = 1.5f * Px;
		FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(R * 2.f, R * 2.f), FSlateLayoutTransform(C - FVector2f(R, R))),
			S->White(), ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Aim ? 0.9f : 0.45f));
	}
	return Layer + 3;
}
