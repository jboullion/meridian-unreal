#include "UI/SMRWorldOverlay.h"

#include "Character/MRCharacter.h"
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
	if (!S || !NetWorld || !Net || !NetWorld->IsActive() || !PC->GetPawn() || Net->GetEffects().bBlind)
	{
		return Layer;  // (blind: nothing of the world shows)
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

	// damage numbers: rising from what was hit and fading (UMRUISubsystem::GetFloaters)
	const double T = Ui->Now();
	const AMRCharacter* MyChar = Cast<AMRCharacter>(Me);
	for (const UMRUISubsystem::FFloater& F : Ui->GetFloaters())
	{
		const double Age = T - F.Start;
		if (Age < 0.0 || Age > UMRUISubsystem::FloaterSeconds)
		{
			continue;
		}
		FVector2f At;
		FVector2D Px2;
		if (F.ObjectId == Net->GetPlayer().Id && MyChar && MyChar->IsFirstPerson())
		{
			At = FVector2f(Geo.GetLocalSize()) * 0.5f + FVector2f(0.f, 40.f * Px);  // under the crosshair
		}
		else
		{
			const AActor* A = F.ObjectId == Net->GetPlayer().Id ? static_cast<const AActor*>(Me) : NetWorld->FindActor(F.ObjectId);
			const FVector Anchor = A == Me ? Me->GetActorLocation() + FVector(0.0, 0.0, Me->GetSimpleCollisionHalfHeight() + 40.0)
				: A ? static_cast<const AMRNetObject*>(A)->GetNameAnchor() : FVector::ZeroVector;
			if (!A || !PC->ProjectWorldLocationToScreen(Anchor, Px2, true))
			{
				continue;
			}
			At = FVector2f(Px2) * ToLocal - FVector2f(0.f, 12.f * Px);  // over its name
		}
		At.Y -= static_cast<float>(Age / UMRUISubsystem::FloaterSeconds) * 26.f * Px;
		const FSlateFontInfo Font = S->Font(16.5f, true);  // (11 until 2026-10-10: half again, to read in a fight)
		const FVector2f M = MRPaint::MeasureText(F.Text, Font);
		FLinearColor Color = F.Color;
		Color.A = FMath::Clamp(static_cast<float>((UMRUISubsystem::FloaterSeconds - Age) / 0.5), 0.f, 1.f);
		MRPaint::Text(Out, Layer + 2, Geo, F.Text, Font, At - FVector2f(M.X * 0.5f, M.Y), Color, Px * 0.8f);
	}

	// a spell or an item waiting for its target: say so under the crosshair
	if (NetWorld->IsChoosingTarget())
	{
		const FString Hint = NetWorld->GetChoosingText() + TEXT("  (click it, \\ for yourself, Esc to stop)");
		const FSlateFontInfo Font = S->Font(10.f, true);
		const FVector2f M = MRPaint::MeasureText(Hint, Font);
		MRPaint::Text(Out, Layer + 2, Geo, Hint, Font, FVector2f(Geo.GetLocalSize()) * 0.5f + FVector2f(-M.X * 0.5f, 18.f * Px),
			FLinearColor(0.75f, 0.85f, 1.f), Px * 0.6f);
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
