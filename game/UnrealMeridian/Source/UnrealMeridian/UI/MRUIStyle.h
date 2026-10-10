#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MRUIStyle.generated.h"

class FJsonObject;
class FPaintArgs;
class FSlateWindowElementList;
class UTexture2D;
struct FGeometry;

/** One frame ("treatment"): the eight corner strips and four repeaters (drawint.c). */
struct FMRFrameBrushes
{
	enum EPiece { UL_H, UL_V, UR_H, UR_V, LL_H, LL_V, LR_H, LR_V, Top, Bottom, Left, Right, Num };
	const FSlateBrush* Piece[Num] = {};
	FVector2f Size[Num];  // Slate units (zero if the frame has no such piece)
	/** Repeater thickness: where content starts. */
	FMargin Inset;
	bool bValid = false;
};

/**
 * The UI's look (docs/adr/0009-user-interface.md): data/ui/ui_style.json plus the art imported
 * by tools/ue/import_ui.py into /Game/Generated/UI (T_UI_<piece>, T_Icon_<bgf>). Brushes are
 * sized in original-client pixels times ui_scale. MRUIReload re-reads the file and rebuilds the
 * widgets (OnReloaded).
 */
UCLASS()
class UNREALMERIDIAN_API UMRUIStyle : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Re-read ui_style.json and drop cached brushes. */
	void Reload();

	/** Slate units per original-client pixel. */
	float Px() const { return UIScale; }
	/** A size in original pixels, in Slate units. */
	float Px(float OriginalPixels) const { return OriginalPixels * UIScale; }

	/** A piece of the original interface art (T_UI_<piece>), or null if it isn't imported. */
	const FSlateBrush* Brush(FName Piece, bool bTile = false, float Scale = 1.f);
	/** An item / spell / skill icon (T_Icon_<bgf>), or null. */
	const FSlateBrush* Icon(FName IconName);
	/** Make sure a texture's real size is known (uncooked textures build asynchronously). */
	static void FinishTexture(class UTexture* Tex);
	/** A render target or other texture as a brush (avatar, minimap). */
	static void SetImage(FSlateBrush& Brush, UObject* Resource, FVector2f Size);
	/** A piece's size in Slate units (zero if missing). */
	FVector2f PieceSize(FName Piece);
	const FMRFrameBrushes& Frame(FName Name);

	FLinearColor Color(const TCHAR* Key, const FLinearColor& Default = FLinearColor::White) const;
	float Number(const TCHAR* Key, float Default) const;
	FSlateFontInfo Font(float Size, bool bBold = false) const;
	/** A per-frame size factor from ui_style.json "frame_scale" (1 if not given). */
	float FrameScale(FName Name) const;
	/** A plain white brush for tinted fills (bars, highlights). */
	const FSlateBrush* White() const { return &WhiteBrush; }

	// --- the HUD over the view (ui_style.json "hud"; after Meridian Shards' Modern interface)
	/** Slate units per HUD pixel (Shards' CSS pixels at 1080p). HUD Size scales each cluster on top (SDPIScaler). */
	float HudPx() const { return HudScale; }
	/** A size in HUD pixels, in Slate units. */
	float HudPx(float HudPixels) const { return HudPixels * HudScale; }
	/** A number from "hud" (in HUD pixels for sizes). */
	float HudNumber(const TCHAR* Key, float Default) const;
	/** A colour from "hud" "colors". */
	FLinearColor HudColor(const TCHAR* Key, const FLinearColor& Default = FLinearColor::White) const;
	/** The HUD's plain text: the UI font at a size in HUD pixels (CSS px). */
	FSlateFontInfo HudFont(float SizePx, bool bBold = false) const;
	/**
	 * Names and titles: Heidelberg, the original client's title face (font.c FONT_TITLES), copied
	 * from the installed client to data/runtime/fonts/heidelb1.ttf by tools/ui/build_ui_art.py;
	 * the UI font when it isn't there. Size in HUD pixels.
	 */
	FSlateFontInfo TitleFont(float SizePx) const;

	FSimpleMulticastDelegate OnReloaded;

private:
	TSharedPtr<FJsonObject> Json;
	float UIScale = 2.f;
	float TextScale = 1.f;
	float HudScale = 1.f;
	/** data/runtime/fonts/heidelb1.ttf as a font, if it's there (null: the UI font stands in). */
	TSharedPtr<const struct FCompositeFont> TitleComposite;
	int32 ArtScale = 4;  // art texels per original pixel (tools/ui/build_ui_art.py SCALE)

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> Textures;
	TMap<FName, TSharedPtr<FSlateBrush>> Brushes;
	TMap<FName, FMRFrameBrushes> Frames;
	FSlateBrush WhiteBrush = FSlateColorBrush(FLinearColor::White);

	UTexture2D* LoadTexture(const FString& Folder, const FString& Name);
};

/** Drawing helpers shared by the widgets (Slate units). */
namespace MRPaint
{
	/** A brush tiled over a box (brush ImageSize = one tile). */
	UNREALMERIDIAN_API void Tile(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FSlateBrush* Brush,
		FVector2f Pos, FVector2f Size, const FLinearColor& Tint = FLinearColor::White);
	/** A brush stretched over a box. */
	UNREALMERIDIAN_API void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FSlateBrush* Brush,
		FVector2f Pos, FVector2f Size, const FLinearColor& Tint = FLinearColor::White);
	/**
	 * A frame over a box: repeaters between the corner strips, corners on top. Without corner
	 * strips (bCornerStrips false) the repeaters run the whole edges: another corner piece covers them.
	 */
	UNREALMERIDIAN_API void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FMRFrameBrushes& Frame,
		FVector2f Pos, FVector2f Size, const FLinearColor& Tint = FLinearColor::White, bool bCornerStrips = true, bool bRepeaters = true);
	/** Text with a one-pixel shadow (Minecraft-style legibility over the world). */
	UNREALMERIDIAN_API void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FString& Text,
		const FSlateFontInfo& Font, FVector2f Pos, const FLinearColor& Color, float Shadow = 1.f);
	UNREALMERIDIAN_API FVector2f MeasureText(const FString& Text, const FSlateFontInfo& Font);
	/** A rounded box: its fill and an outline drawn inside its edge (OutlineWidth 0: none). */
	UNREALMERIDIAN_API void Rounded(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, FVector2f Pos, FVector2f Size,
		const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
	/** A vertical gradient from Top to Bottom with rounded corners. */
	UNREALMERIDIAN_API void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, FVector2f Pos, FVector2f Size,
		const FLinearColor& Top, const FLinearColor& Bottom, float Radius = 0.f);
	/** A soft shadow under a box (CSS box-shadow 0 Y Blur): a few widening, fading rounded boxes. */
	UNREALMERIDIAN_API void SoftShadow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, FVector2f Pos, FVector2f Size,
		float Radius, float OffsetY, float Blur, const FLinearColor& Color);
	/** The HUD's panel (Shards' --hud-panel and --hud-edge): shadow, translucent dark fill, thin gold edge. Radius in Slate units. */
	UNREALMERIDIAN_API void HudPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, UMRUIStyle* Style, FVector2f Pos,
		FVector2f Size, float Radius, float Opacity = 1.f, const FLinearColor* Edge = nullptr);
}
