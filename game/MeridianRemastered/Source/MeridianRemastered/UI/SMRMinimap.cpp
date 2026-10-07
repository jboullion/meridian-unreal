#include "UI/SMRMinimap.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Player/MRPlayerState.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "Zones/MRZoneSubsystem.h"

#define LOCTEXT_NAMESPACE "MRMinimap"

// ------------------------------------------------------------------------------ data

namespace MRMinimap
{
	namespace
	{
		TSharedPtr<FJsonObject> LoadSettings()
		{
			static TSharedPtr<FJsonObject> Cached;
			static double LoadedAt = -1.0;
			// re-read now and then so MRUIReload / edits show up without a restart
			if (!Cached.IsValid() || FPlatformTime::Seconds() - LoadedAt > 5.0)
			{
				FString Text;
				TSharedPtr<FJsonObject> Root;
				const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ui"), TEXT("minimap.json"));
				Cached = FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root
					? Root : MakeShared<FJsonObject>();
				LoadedAt = FPlatformTime::Seconds();
			}
			return Cached;
		}

		void Apply(const FJsonObject& O, FZoneSettings& S)
		{
			double V;
			if (O.TryGetNumberField(TEXT("margin"), V)) S.Margin = static_cast<float>(V);
			if (O.TryGetNumberField(TEXT("cut_height_cm"), V)) S.CutHeightCm = static_cast<float>(V);
			if (O.TryGetNumberField(TEXT("resolution"), V)) S.Resolution = static_cast<int32>(V);
			O.TryGetBoolField(TEXT("skip"), S.bSkip);
			O.TryGetBoolField(TEXT("hide_props"), S.bHideProps);
			O.TryGetBoolField(TEXT("shadows"), S.bShadows);
			FString Source;
			if (O.TryGetStringField(TEXT("source"), Source))
			{
				S.bBaseColor = Source == TEXT("basecolor");
			}
		}
	}

	FZoneSettings Settings(int32 GeometryRid)
	{
		FZoneSettings S;
		const TSharedPtr<FJsonObject> Root = LoadSettings();
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Root->TryGetObjectField(TEXT("defaults"), Obj))
		{
			Apply(**Obj, S);
		}
		const TSharedPtr<FJsonObject>* Zones = nullptr;
		if (Root->TryGetObjectField(TEXT("zones"), Zones) && (*Zones)->TryGetObjectField(FString::FromInt(GeometryRid), Obj))
		{
			Apply(**Obj, S);
		}
		return S;
	}

	bool CaptureRect(const UMRZoneSubsystem* Zones, int32 GeometryRid, FBox2D& Out)
	{
		if (!Zones)
		{
			return false;
		}
		FBox2D Box(ForceInit);
		for (const TPair<int32, FMRZoneInfo>& Z : Zones->GetZones())
		{
			if (Z.Value.GeometryRid == GeometryRid && Z.Value.BoundsWorld.bIsValid)
			{
				Box += Z.Value.BoundsWorld;
			}
		}
		if (!Box.bIsValid)
		{
			return false;
		}
		const FZoneSettings S = Settings(GeometryRid);
		const FVector2D Size = Box.GetSize();
		const double Side = FMath::Max(Size.X, Size.Y) * (1.0 + 2.0 * S.Margin);
		const FVector2D C = Box.GetCenter();
		Out = FBox2D(C - FVector2D(Side * 0.5), C + FVector2D(Side * 0.5));
		return true;
	}

	FString TexturePath(int32 GeometryRid)
	{
		return FString::Printf(TEXT("/Game/Generated/UI/Minimap/T_Map_%d.T_Map_%d"), GeometryRid, GeometryRid);
	}

	FString Style()
	{
		FString S = TEXT("photo");
		LoadSettings()->TryGetStringField(TEXT("style"), S);
		return S;
	}
}

// ------------------------------------------------------------------------------ widget

void SMRMinimap::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	SetVisibility(EVisibility::HitTestInvisible);
}

FVector2D SMRMinimap::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float Px = S ? S->Px() : 2.f;
	const float Side = (S ? S->Number(TEXT("minimap_px"), 96.f) : 96.f) * Px;
	return FVector2D(Side, Side + 22.f * Px);  // and two lines of text under it
}

void SMRMinimap::Tick(const FGeometry& Geo, const double Time, const float Dt)
{
	UMRUISubsystem* Ui = UI.Get();
	APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const UMRZoneSubsystem* Zones = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return;
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
	ZoneName = Zone ? FText::FromString(Zone->Name) : FText::GetEmpty();
	const int32 Geom = Zone ? Zone->GeometryRid : 0;
	if (Geom != GeometryRid)
	{
		GeometryRid = Geom;
		Rect = FBox2D(ForceInit);
		MRMinimap::CaptureRect(Zones, Geom, Rect);
		UTexture2D* Tex = Ui->GetMapTexture(Geom);
		MapBrush = FSlateBrush();
		if (Tex)
		{
			UMRUIStyle::SetImage(MapBrush, Tex, FVector2f(Tex->GetSizeX(), Tex->GetSizeY()));
		}
	}
	Zoom = FMath::FInterpTo(Zoom, Ui->GetMapZoom(), Dt, 12.f);
}

int32 SMRMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	if (!S || !PC)
	{
		return Layer;
	}
	const float Px = S->Px();
	const FLinearColor Tint = WStyle.GetColorAndOpacityTint();
	const float Side = S->Number(TEXT("minimap_px"), 96.f) * Px;
	const FVector2f MapSize(Side, Side);

	// the parchment, under everything (and around the picture's edges)
	MRPaint::Tile(Out, Layer, Geo, S->Brush(TEXT("mapbkgnd"), true), FVector2f::ZeroVector, MapSize, Tint);

	const APawn* Pawn = PC->GetPawn();
	const FVector2D Player = Pawn ? FVector2D(Pawn->GetActorLocation()) : FVector2D::ZeroVector;
	if (MapBrush.GetResourceObject() && Rect.bIsValid)
	{
		// the visible world square around the player, intersected with the picture
		// at zoom 1 the view is minimap_span_m across, or the whole picture for a smaller room
		const double Base = FMath::Min<double>(S->Number(TEXT("minimap_span_m"), 50.f) * 100.0, Rect.GetSize().X);
		const double Span = Base / FMath::Max(0.25f, Zoom);
		const FBox2D View(Player - FVector2D(Span * 0.5), Player + FVector2D(Span * 0.5));
		const FBox2D Part(FVector2D(FMath::Max(View.Min.X, Rect.Min.X), FMath::Max(View.Min.Y, Rect.Min.Y)),
			FVector2D(FMath::Min(View.Max.X, Rect.Max.X), FMath::Min(View.Max.Y, Rect.Max.Y)));
		if (Part.Max.X > Part.Min.X && Part.Max.Y > Part.Min.Y)
		{
			const FVector2D RectSize = Rect.GetSize();
			MapBrush.SetUVRegion(FBox2f(FVector2f((Part.Min - Rect.Min) / RectSize), FVector2f((Part.Max - Rect.Min) / RectSize)));
			const FVector2f Pos = FVector2f((Part.Min - View.Min) / Span) * Side;
			const FVector2f Size = FVector2f(Part.GetSize() / Span) * Side;
			MRPaint::Box(Out, Layer + 1, Geo, &MapBrush, Pos, Size, Tint);
		}
	}
	else
	{
		const FString Missing = LOCTEXT("NoMap", "no map").ToString();
		const FSlateFontInfo Font = S->Font(8.f);
		const FVector2f M = MRPaint::MeasureText(Missing, Font);
		MRPaint::Text(Out, Layer + 1, Geo, Missing, Font, (MapSize - M) * 0.5f + FVector2f(0.f, Side * 0.2f), FLinearColor(0.2f, 0.18f, 0.15f, 0.8f) * Tint, 0.f);
	}

	// the player: an arrow turned to the view's heading (yaw 0 = east = right; the art points north)
	if (const FSlateBrush* Arrow = S->Brush(TEXT("map_arrow")))
	{
		const FVector2f A = FVector2f(Arrow->ImageSize) * S->Number(TEXT("minimap_arrow_scale"), 1.f);
		const float Yaw = PC->GetControlRotation().Yaw;
		FSlateDrawElement::MakeRotatedBox(Out, Layer + 2, Geo.ToPaintGeometry(A, FSlateLayoutTransform((MapSize - A) * 0.5f)), Arrow,
			ESlateDrawEffect::None, FMath::DegreesToRadians(Yaw + 90.f), TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Tint);
	}

	// the original map's metal rim
	MRPaint::Frame(Out, Layer + 3, Geo, S->Frame(TEXT("map")), FVector2f::ZeroVector, MapSize, Tint);

	// zone name and the Meridian time under the map
	const FLinearColor TextColor = S->Color(TEXT("text"), FLinearColor(1.f, 0.93f, 0.7f)) * Tint;
	FString Clock;
	if (const UMRGameTimeSubsystem* Time = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRGameTimeSubsystem>() : nullptr)
	{
		const double H = Time->GetGameHour();
		const int32 Hour = FMath::FloorToInt(H) % 24;
		const int32 Min = FMath::FloorToInt(FMath::Frac(H) * 60.0);
		Clock = FString::Printf(TEXT("%d:%02d %s"), Hour % 12 == 0 ? 12 : Hour % 12, Min, Hour < 12 ? TEXT("AM") : TEXT("PM"));
	}
	const FSlateFontInfo NameFont = S->Font(9.f, true), TimeFont = S->Font(8.f);
	const FString Name = ZoneName.ToString();
	const FVector2f NM = MRPaint::MeasureText(Name, NameFont), TM = MRPaint::MeasureText(Clock, TimeFont);
	MRPaint::Text(Out, Layer + 5, Geo, Name, NameFont, FVector2f((Side - NM.X) * 0.5f, Side + 3.f * Px), TextColor, Px * 0.5f);
	MRPaint::Text(Out, Layer + 5, Geo, Clock, TimeFont, FVector2f((Side - TM.X) * 0.5f, Side + 3.f * Px + NM.Y), TextColor * FLinearColor(0.85f, 0.85f, 0.85f, 1.f), Px * 0.5f);
	return Layer + 7;
}

#undef LOCTEXT_NAMESPACE
