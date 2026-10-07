#include "UI/MRUIStyle.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif
#include "Zones/MRZoneSubsystem.h"

namespace
{
	const TCHAR* ArtFolder = TEXT("/Game/Generated/UI/Art");
	const TCHAR* IconFolder = TEXT("/Game/Generated/UI/Icons");

	const TCHAR* FrameSlots[FMRFrameBrushes::Num] = {
		TEXT("ul_h"), TEXT("ul_v"), TEXT("ur_h"), TEXT("ur_v"), TEXT("ll_h"), TEXT("ll_v"), TEXT("lr_h"), TEXT("lr_v"),
		TEXT("top"), TEXT("bottom"), TEXT("left"), TEXT("right")};
}

void UMRUIStyle::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Reload();
}

void UMRUIStyle::Reload()
{
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ui"), TEXT("ui_style.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root)
	{
		Json = Root;
	}
	else
	{
		UE_LOG(LogMeridian, Warning, TEXT("UI style %s not found; using defaults"), *Path);
		Json = MakeShared<FJsonObject>();
	}
	UIScale = Number(TEXT("ui_scale"), 2.f);
	Brushes.Reset();
	Frames.Reset();
	OnReloaded.Broadcast();
}

UTexture2D* UMRUIStyle::LoadTexture(const FString& Folder, const FString& Name)
{
	const FName Key(*(Folder + TEXT("/") + Name));
	if (const TObjectPtr<UTexture2D>* Found = Textures.Find(Key))
	{
		return *Found;
	}
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	FinishTexture(Tex);
	Textures.Add(Key, Tex);  // remember misses too
	return Tex;
}

const FSlateBrush* UMRUIStyle::Brush(FName Piece, bool bTile)
{
	const FName Key(*FString::Printf(TEXT("%s%s"), *Piece.ToString(), bTile ? TEXT("#tile") : TEXT("")));
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Key))
	{
		return Found->Get();
	}
	UTexture2D* Tex = LoadTexture(ArtFolder, TEXT("T_UI_") + Piece.ToString());
	TSharedPtr<FSlateBrush> B;
	if (Tex)
	{
		B = MakeShared<FSlateBrush>();
		const FVector2f Size(Tex->GetSizeX() / float(ArtScale) * UIScale, Tex->GetSizeY() / float(ArtScale) * UIScale);
		SetImage(*B, Tex, Size);
		B->Tiling = bTile ? ESlateBrushTileType::Both : ESlateBrushTileType::NoTile;
		UE_LOG(LogMeridian, Verbose, TEXT("UI brush %s: texture %dx%d, %.1f x %.1f units%s"), *Piece.ToString(), Tex->GetSizeX(), Tex->GetSizeY(),
			Size.X, Size.Y, bTile ? TEXT(", tiled") : TEXT(""));
	}
	Brushes.Add(Key, B);
	return B.Get();
}

const FSlateBrush* UMRUIStyle::Icon(FName IconName)
{
	if (IconName.IsNone())
	{
		return nullptr;
	}
	const FName Key(*(TEXT("icon:") + IconName.ToString()));
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Key))
	{
		return Found->Get();
	}
	UTexture2D* Tex = LoadTexture(IconFolder, TEXT("T_Icon_") + IconName.ToString());
	TSharedPtr<FSlateBrush> B;
	if (Tex)
	{
		B = MakeShared<FSlateBrush>();
		SetImage(*B, Tex, FVector2f(Tex->GetSizeX(), Tex->GetSizeY()));
	}
	Brushes.Add(Key, B);
	return B.Get();
}

void UMRUIStyle::FinishTexture(UTexture* Tex)
{
#if WITH_EDITOR
	// uncooked (editor or -game from the editor): textures build asynchronously and report a
	// 32 x 32 placeholder until done, and brush sizes come from the real size
	if (Tex)
	{
		UTexture* const List[] = {Tex};
		FTextureCompilingManager::Get().FinishCompilation(List);
	}
#endif
}

void UMRUIStyle::SetImage(FSlateBrush& Brush, UObject* Resource, FVector2f Size)
{
	Brush.SetResourceObject(Resource);
	Brush.ImageSize = FVector2D(Size);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageType = ESlateBrushImageType::FullColor;
}

FVector2f UMRUIStyle::PieceSize(FName Piece)
{
	const FSlateBrush* B = Brush(Piece);
	return B ? FVector2f(B->ImageSize) : FVector2f::ZeroVector;
}

const FMRFrameBrushes& UMRUIStyle::Frame(FName Name)
{
	if (const FMRFrameBrushes* Found = Frames.Find(Name))
	{
		return *Found;
	}
	FMRFrameBrushes F;
	for (int32 i = 0; i < FMRFrameBrushes::Num; ++i)
	{
		const FName Piece(*FString::Printf(TEXT("%s_%s"), *Name.ToString(), FrameSlots[i]));
		F.Piece[i] = Brush(Piece, i >= FMRFrameBrushes::Top);
		F.Size[i] = F.Piece[i] ? FVector2f(F.Piece[i]->ImageSize) : FVector2f::ZeroVector;
		F.bValid |= F.Piece[i] != nullptr;
	}
	F.Inset = FMargin(F.Size[FMRFrameBrushes::Left].X, F.Size[FMRFrameBrushes::Top].Y,
		F.Size[FMRFrameBrushes::Right].X, F.Size[FMRFrameBrushes::Bottom].Y);
	return Frames.Add(Name, F);
}

FLinearColor UMRUIStyle::Color(const TCHAR* Key, const FLinearColor& Default) const
{
	const TSharedPtr<FJsonObject>* Colors = nullptr;
	FString Hex;
	if (Json && Json->TryGetObjectField(TEXT("colors"), Colors) && (*Colors)->TryGetStringField(Key, Hex))
	{
		return FLinearColor(FColor::FromHex(Hex));
	}
	return Default;
}

float UMRUIStyle::Number(const TCHAR* Key, float Default) const
{
	double V = Default;
	if (Json && Json->TryGetNumberField(Key, V))
	{
		return static_cast<float>(V);
	}
	const TSharedPtr<FJsonObject>* Numbers = nullptr;
	if (Json && Json->TryGetObjectField(TEXT("numbers"), Numbers) && (*Numbers)->TryGetNumberField(Key, V))
	{
		return static_cast<float>(V);
	}
	return Default;
}

FSlateFontInfo UMRUIStyle::Font(float Size, bool bBold) const
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size * UIScale * 0.5f);
}

// ------------------------------------------------------------------------------ painting

namespace MRPaint
{
	void Tile(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FSlateBrush* Brush, FVector2f Pos, FVector2f Size, const FLinearColor& Tint)
	{
		if (!Brush || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		// Slate tiles by the texture's pixels, not the brush's ImageSize (ElementBatcher: LocalSize /
		// TextureWidth): draw a box k times larger, scaled back down by k, so one tile = ImageSize
		const UTexture2D* Tex = Brush->Tiling != ESlateBrushTileType::NoTile ? Cast<UTexture2D>(Brush->GetResourceObject()) : nullptr;
		const float K = Tex && Brush->ImageSize.X > 0.f ? Tex->GetSizeX() / static_cast<float>(Brush->ImageSize.X) : 1.f;
		if (!FMath::IsNearlyEqual(K, 1.f, 1e-3f) && K > 0.f)
		{
			FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(Size * K, FSlateLayoutTransform(Pos), FSlateRenderTransform(1.f / K),
				FVector2f::ZeroVector), Brush, ESlateDrawEffect::None, Tint);
			return;
		}
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Brush, ESlateDrawEffect::None, Tint);
	}

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FSlateBrush* Brush, FVector2f Pos, FVector2f Size, const FLinearColor& Tint)
	{
		Tile(Out, Layer, Geo, Brush, Pos, Size, Tint);
	}

	void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FMRFrameBrushes& F, FVector2f Pos, FVector2f Size, const FLinearColor& Tint)
	{
		if (!F.bValid)
		{
			return;
		}
		using P = FMRFrameBrushes;
		const FVector2f* S = F.Size;
		const float X0 = Pos.X, Y0 = Pos.Y, X1 = Pos.X + Size.X, Y1 = Pos.Y + Size.Y;
		auto Draw = [&](int32 i, float X, float Y, float W, float H)
		{
			Tile(Out, Layer, Geo, F.Piece[i], FVector2f(X, Y), FVector2f(W, H), Tint);
		};
		// repeaters between the corner strips (drawint.c ELEMENT_ETOP...)
		const float TopX0 = X0 + FMath::Max(S[P::UL_H].X, S[P::UL_V].X), TopX1 = X1 - FMath::Max(S[P::UR_H].X, S[P::UR_V].X);
		const float BotX0 = X0 + FMath::Max(S[P::LL_H].X, S[P::LL_V].X), BotX1 = X1 - FMath::Max(S[P::LR_H].X, S[P::LR_V].X);
		const float LefY0 = Y0 + S[P::UL_H].Y + S[P::UL_V].Y, LefY1 = Y1 - S[P::LL_H].Y - S[P::LL_V].Y;
		const float RigY0 = Y0 + S[P::UR_H].Y + S[P::UR_V].Y, RigY1 = Y1 - S[P::LR_H].Y - S[P::LR_V].Y;
		Draw(P::Top, TopX0, Y0, TopX1 - TopX0, S[P::Top].Y);
		Draw(P::Bottom, BotX0, Y1 - S[P::Bottom].Y, BotX1 - BotX0, S[P::Bottom].Y);
		Draw(P::Left, X0, LefY0, S[P::Left].X, LefY1 - LefY0);
		Draw(P::Right, X1 - S[P::Right].X, RigY0, S[P::Right].X, RigY1 - RigY0);
		// corner strips: along the edge from the corner, and down the side below / above it
		auto Corner = [&](int32 i, float X, float Y)
		{
			Tile(Out, Layer + 1, Geo, F.Piece[i], FVector2f(X, Y), S[i], Tint);
		};
		Corner(P::UL_H, X0, Y0);
		Corner(P::UL_V, X0, Y0 + S[P::UL_H].Y);
		Corner(P::UR_H, X1 - S[P::UR_H].X, Y0);
		Corner(P::UR_V, X1 - S[P::UR_V].X, Y0 + S[P::UR_H].Y);
		Corner(P::LL_H, X0, Y1 - S[P::LL_H].Y);
		Corner(P::LL_V, X0, Y1 - S[P::LL_H].Y - S[P::LL_V].Y);
		Corner(P::LR_H, X1 - S[P::LR_H].X, Y1 - S[P::LR_H].Y);
		Corner(P::LR_V, X1 - S[P::LR_V].X, Y1 - S[P::LR_H].Y - S[P::LR_V].Y);
	}

	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FString& Str, const FSlateFontInfo& Font, FVector2f Pos, const FLinearColor& Color, float Shadow)
	{
		const FVector2f Size = MeasureText(Str, Font);
		if (Shadow > 0.f)
		{
			FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(Size, FSlateLayoutTransform(Pos + FVector2f(Shadow, Shadow))), Str, Font,
				ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.85f));
		}
		FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Str, Font, ESlateDrawEffect::None, Color);
	}

	FVector2f MeasureText(const FString& Str, const FSlateFontInfo& Font)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return FVector2f::ZeroVector;
		}
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, Font));
	}
}

// ------------------------------------------------------------------------------ console

static FAutoConsoleCommandWithWorld GMRUIReload(TEXT("MRUIReload"),
	TEXT("Re-read data/ui/ui_style.json and rebuild the UI."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			if (UMRUIStyle* Style = World->GetGameInstance()->GetSubsystem<UMRUIStyle>())
			{
				Style->Reload();
			}
		}
	}));
