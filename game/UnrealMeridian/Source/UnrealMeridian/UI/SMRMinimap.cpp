#include "UI/SMRMinimap.h"
#include "Net/MRNetSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
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
	SideArg = InArgs._Side;
	bWhole = InArgs._bWhole;
	OnClicked = InArgs._OnClicked;
	SetVisibility(OnClicked.IsBound() ? EVisibility::Visible : EVisibility::HitTestInvisible);
}

float SMRMinimap::SidePx() const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float Px = S ? S->Px() : 2.f;
	return (SideArg > 0.f ? SideArg : S ? S->Number(TEXT("minimap_px"), 96.f) : 96.f) * Px;
}

FVector2D SMRMinimap::ComputeDesiredSize(float) const
{
	const UMRUIStyle* S = UI.IsValid() ? UI->GetStyle() : nullptr;
	const float Px = S ? S->Px() : 2.f;
	const float SideNow = SidePx();
	return FVector2D(SideNow, SideNow + 22.f * Px);  // and two lines of text under it
}

FBox2D SMRMinimap::ViewBox() const
{
	const UMRUISubsystem* Ui = UI.Get();
	const UMRUIStyle* S = Ui ? Ui->GetStyle() : nullptr;
	const APlayerController* PC = Ui ? Ui->GetPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (bWhole)
	{
		// the whole room: its picture's square, else its walls' box made square
		FBox2D Box = Rect;
		const UMRZoneSubsystem* Zones = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
		const FMRZoneInfo* Zone = Zones ? Zones->FindZone(GeometryRid) : nullptr;
		if (!Box.bIsValid && Zone)
		{
			Box = FBox2D(ForceInit);
			for (const FVector4& W : Zone->MapWalls)
			{
				Box += FVector2D(W.X, W.Y);
				Box += FVector2D(W.Z, W.W);
			}
		}
		if (Box.bIsValid)
		{
			const double Half = FMath::Max(Box.GetSize().X, Box.GetSize().Y) * 0.55;
			return FBox2D(Box.GetCenter() - FVector2D(Half), Box.GetCenter() + FVector2D(Half));
		}
	}
	const FVector2D Player = Pawn ? FVector2D(Pawn->GetActorLocation()) : FVector2D::ZeroVector;
	// the visible world square around the player: at zoom 1 minimap_span_m across, or the whole
	// picture for a smaller room
	const double Base = FMath::Min<double>((S ? S->Number(TEXT("minimap_span_m"), 50.f) : 50.f) * 100.0, Rect.bIsValid ? Rect.GetSize().X : 1e9);
	const double Span = Base / FMath::Max(0.25f, Zoom);
	return FBox2D(Player - FVector2D(Span * 0.5), Player + FVector2D(Span * 0.5));
}

FReply SMRMinimap::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!OnClicked.IsBound() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2f Local = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const float SideNow = SidePx();
	if (Local.X < 0.f || Local.Y < 0.f || Local.X > SideNow || Local.Y > SideNow)
	{
		return FReply::Unhandled();
	}
	const FBox2D View = ViewBox();
	OnClicked.Execute(View.Min + FVector2D(Local / SideNow) * View.GetSize().X);
	return FReply::Handled();
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
	const float Side = SidePx();
	const FVector2f MapSize(Side, Side);

	// the parchment, under everything (and around the picture's edges)
	MRPaint::Tile(Out, Layer, Geo, S->Brush(TEXT("mapbkgnd"), true), FVector2f::ZeroVector, MapSize, Tint);

	const APawn* Pawn = PC->GetPawn();
	const FVector2D Player = Pawn ? FVector2D(Pawn->GetActorLocation()) : FVector2D::ZeroVector;
	const FBox2D View = ViewBox();
	const double Span = View.GetSize().X;
	auto ToMap = [&View, Span, Side](const FVector2D& World) { return FVector2f((World - View.Min) / Span) * Side; };
	const UMRZoneSubsystem* Zones = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	const FMRZoneInfo* Zone = Zones ? Zones->FindZone(GeometryRid) : nullptr;
	if (!MapBrush.GetResourceObject() && Zone && Zone->MapWalls.Num() > 0)
	{
		// a room built at runtime: the original map's wall lines, in ink on the parchment
		const FLinearColor Ink = FLinearColor(0.16f, 0.11f, 0.07f, 0.9f) * Tint;
		const FBox2D Clip(View.Min - FVector2D(Span * 0.1), View.Max + FVector2D(Span * 0.1));
		for (const FVector4& W : Zone->MapWalls)
		{
			const FVector2D A(W.X, W.Y), B(W.Z, W.W);
			if (!Clip.IsInside(A) && !Clip.IsInside(B))
			{
				continue;
			}
			TArray<FVector2f> Line = {ToMap(A), ToMap(B)};
			for (FVector2f& P : Line)
			{
				P = FVector2f(FMath::Clamp(P.X, 0.f, Side), FMath::Clamp(P.Y, 0.f, Side));
			}
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), Line, ESlateDrawEffect::None, Ink, true, FMath::Max(1.f, Px * 0.6f));
		}
	}
	else if (MapBrush.GetResourceObject() && Rect.bIsValid)
	{
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
	else if (!Zone || Zone->MapWalls.Num() == 0)
	{
		const FString Missing = LOCTEXT("NoMap", "no map").ToString();
		const FSlateFontInfo Font = S->Font(8.f);
		const FVector2f M = MRPaint::MeasureText(Missing, Font);
		MRPaint::Text(Out, Layer + 1, Geo, Missing, Font, (MapSize - M) * 0.5f + FVector2f(0.f, Side * 0.2f), FLinearColor(0.2f, 0.18f, 0.15f, 0.8f) * Tint, 0.f);
	}

	// the server's objects with a minimap dot (proto.h MM_*): players blue, enemies red, guild friends
	// green, monsters dark red, NPCs gold
	if (const UMRNetWorldSubsystem* NetWorld = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr)
	{
		const float Dot = FMath::Max(2.f, 2.2f * Px);
		for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
		{
			const AMRNetObject* A = Pair.Value.Get();
			const uint32 MM = A ? A->GetMinimapFlags() : 0;
			if (!MM || A->GetDrawEffect() == MRMsg::DRAWFX_INVISIBLE || !View.IsInside(FVector2D(A->GetActorLocation())))
			{
				continue;
			}
			FLinearColor C(0.1f, 0.25f, 0.9f);                                           // MM_PLAYER
			if (MM & MRMsg::MM_ENEMY) C = FLinearColor(0.9f, 0.05f, 0.05f);
			else if (MM & (MRMsg::MM_FRIEND | MRMsg::MM_GUILDMATE)) C = FLinearColor(0.1f, 0.7f, 0.15f);
			else if (MM & MRMsg::MM_NPC) C = FLinearColor(0.95f, 0.75f, 0.1f);
			else if (MM & MRMsg::MM_MONSTER) C = MM & MRMsg::MM_BOSS ? FLinearColor(0.75f, 0.1f, 0.8f) : FLinearColor(0.55f, 0.05f, 0.05f);
			else if (!(MM & MRMsg::MM_PLAYER)) C = FLinearColor(0.35f, 0.3f, 0.25f);
			const FVector2f P = ToMap(FVector2D(A->GetActorLocation()));
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Dot, Dot), FSlateLayoutTransform(P - FVector2f(Dot * 0.5f))),
				S->White(), ESlateDrawEffect::None, C * Tint);
		}
	}

	// the player's notes on the large map (the original's annotations): a numbered pin each, its text beside it
	if (bWhole && Zone)
	{
		const UMRNetSubsystem* Net = Ui->GetNet();
		const TArray<FMRMapNote>* Notes = Net ? Net->GetSocial().MapNotes.Find(Net->GetPlayer().RoomFile.ToLower()) : nullptr;
		const FSlateFontInfo NoteFont = S->Font(8.5f, true);
		for (int32 i = 0; Notes && i < Notes->Num(); ++i)
		{
			const FMRMapNote& N = (*Notes)[i];
			const FVector2f P = ToMap(FVector2D(Zone->Origin) + FVector2D(N.X, N.Y));
			const float Pin = FMath::Max(4.f, 3.f * Px);
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(Pin, Pin), FSlateLayoutTransform(P - FVector2f(Pin * 0.5f))),
				S->White(), ESlateDrawEffect::None, FLinearColor(0.75f, 0.05f, 0.05f) * Tint);
			MRPaint::Text(Out, Layer + 4, Geo, FString::Printf(TEXT("%d %s"), i + 1, *N.Text), NoteFont, P + FVector2f(Pin, -Pin),
				FLinearColor(1.f, 0.9f, 0.6f) * Tint, Px * 0.5f);
		}
	}

	// the player: an arrow turned to the view's heading (yaw 0 = east = right; the art points north)
	if (const FSlateBrush* Arrow = S->Brush(TEXT("map_arrow")))
	{
		const FVector2f A = FVector2f(Arrow->ImageSize) * S->Number(TEXT("minimap_arrow_scale"), 1.f);
		const float Yaw = PC->GetControlRotation().Yaw;
		const FVector2f At = bWhole ? ToMap(Player) - A * 0.5f : (MapSize - A) * 0.5f;
		FSlateDrawElement::MakeRotatedBox(Out, Layer + 2, Geo.ToPaintGeometry(A, FSlateLayoutTransform(At)), Arrow,
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
