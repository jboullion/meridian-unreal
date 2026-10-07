#pragma once

#include "CoreMinimal.h"

/**
 * Sprite players (docs/sprites.md): the original client's player model, read from
 * data/sprites/player_parts.json (tools/sprites/build_player_sprites.py) and
 * data/sprites/player_actions.json, plus ports of the client functions that place and animate it.
 * Pure data and math, no UObjects: UMRSpriteBodyComponent draws it, the tests check it against the
 * Python port (tools/sprites/m59sprites.py).
 *
 * "Base pixels" are pixels of the torso bitmap (x right, y down, origin at its top-left); groups
 * here are 0-based like the client's (Kod's are 1-based).
 */

struct FMRSpriteBitmap
{
	int32 W = 0, H = 0, XOff = 0, YOff = 0;
	/** Hotspot number (negative = underlay) -> position in this bitmap's pixels. */
	TArray<TPair<int32, FIntPoint>> Hotspots;
};

struct FMRSpriteBgf
{
	int32 Shrink = 1;
	TArray<TArray<int32>> Groups;
	TArray<FMRSpriteBitmap> Bitmaps;

	/** GetObjectPdib: the bitmap of a group seen at a relative angle (0..4095, 0 = facing the viewer); -1 if none. */
	int32 BitmapIndex(int32 Group, int32 Angle) const;

	/** In-betweens from bitmap A to B (tools/sprites/tweens.py), extra bitmaps after the originals. */
	TMap<uint64, TArray<int32>> Tweens;
	const TArray<int32>* FindTweens(int32 A, int32 B) const { return Tweens.Find((uint64(uint32(A)) << 32) | uint32(B)); }
};

struct FMRSpriteAtlas
{
	FString Texture;     // asset name in /Game/Generated/Sprites
	FIntPoint Size = FIntPoint::ZeroValue;
	TMap<int32, FIntRect> Cells;  // bitmap index -> texels (min inclusive, max exclusive)
};

struct FMRSpritePart
{
	FName Name;          // body, left_arm, right_arm, legs, head, mouth, eyes, nose, hair, weapon
	FString Bgf;
	FString Atlas;       // key into FMRSpriteLibrary::Atlases (the bgf: atlases are untranslated)
	int32 Hotspot = 0;   // where it attaches (0 = the body itself)
	int32 Xlat = 0;      // the look's palette translation for this part (xlat.h ids)
	int32 Class = 0;     // surface class (data/sprites/materials.json)
};

/** A surface class: how M_SpriteBody shades a part (data/sprites/materials.json). */
struct FMRSpriteMaterialClass
{
	FString Name;
	float Roughness = 0.85f, Metallic = 0.f, Specular = 0.2f;
};

/**
 * The original character creator's colours (xlat.c, kod/include/blakston.khd, player.kod):
 * skin = PT_BLUE_TO_SKIN1-4, hair = the creator's 14 hair translations, shirt / pants = XLAT_TO_*
 * ramps, worn as two-colour translations (red -> the clothes colour, blue -> the skin's ramp).
 */
struct MERIDIANREMASTERED_API FMRSpriteColours
{
	static constexpr int32 NumSkins = 4;
	static constexpr int32 NumHair = 14;
	static constexpr int32 NumClothes = 11;
	static const TCHAR* SkinName(int32 Skin);
	static const TCHAR* HairName(int32 Hair);
	static const TCHAR* ClothesName(int32 Colour);
	/** Face translation of a skin (0..3). */
	static int32 SkinXlat(int32 Skin);
	static int32 HairXlat(int32 Hair);
	/** EncodeTwoColorXLAT: clothes colour (0..10) worn with a skin (0..3). */
	static int32 ClothesXlat(int32 Colour, int32 Skin);
	/** The clothes colour of a two-colour translation, or -1. */
	static int32 ClothesOf(int32 Xlat);
};

struct FMRSpriteLook
{
	FName Name;
	/** Overlays in the order Kod sends them (SendOverlays), then the body last. */
	TArray<FMRSpritePart> Parts;
	int32 ActionFace = 1;            // piAction: the eyes' and mouth's Kod group
	/** Base pixels around the feet that hold every frame: render target box. */
	FBox2f Bounds = FBox2f(FVector2f(-100.f, -240.f), FVector2f(100.f, 4.f));

	const FMRSpritePart* Find(FName Part) const;
};

/** One part's animation in an action (Kod's ANIMATE_NONE / CYCLE / ONCE, 1-based groups). */
struct FMRSpriteTrackDef
{
	enum class EMode : uint8 { None, Cycle, Once };
	EMode Mode = EMode::None;
	int32 PeriodMs = 0;
	int32 Low = 1, High = 1, Final = 1;
};

struct FMRSpriteAction
{
	FName Name;
	TMap<FName, FMRSpriteTrackDef> Tracks;
	/** Length of a one-shot action (the longest 'once' track), 0 for loops / poses. */
	int32 OnceLengthMs() const;
};

/** A running track: clientd3d/animate.c AnimateSingle. Groups are Kod's (1-based). */
struct FMRSpriteTrack
{
	FMRSpriteTrackDef Def;
	int32 Group = 1;
	float TickMs = 0.f;

	void Start(const FMRSpriteTrackDef& InDef);
	/** Advance; true if the group changed. */
	bool Step(float DtMs);
	bool IsPlaying() const { return Def.Mode != FMRSpriteTrackDef::EMode::None; }
	/** 0..1 through the current group (for smoothing between groups). */
	float Phase() const { return Def.PeriodMs > 0 ? FMath::Clamp(1.f - TickMs / Def.PeriodMs, 0.f, 1.f) : 0.f; }
	/** The group after this one (what Step will show next), or the current one at the end. */
	int32 NextGroup() const;
};

/** A first-person hand or weapon (a "window overlay"): its bgf, the group it rests on (0 = none) and its attack. */
struct FMRFirstPersonOverlay
{
	FString Bgf;
	int32 Hold = 0;
	FMRSpriteTrackDef Attack;
};

/**
 * A monster or NPC class (tools/sprites/monsters.py, from data/monsters.json and its Kod): its
 * sprite looks and what its simple AI needs.
 */
struct FMRMonsterDef
{
	FName Class;
	FString Name;                 // vrName ("giant rat")
	FName Look;                   // m_<Class>
	FName DeadLook;               // m_<Class>_dead, the corpse (None: none)
	float SpeedCms = 100.f;       // viSpeed in the remaster's units
	float VisionCm = 2200.f;      // viVisionDistance
	bool bAggressive = false;     // AI_FIGHT_AGGRESSIVE
	bool bNpc = false;
	bool bStationary = false;     // AI_NOMOVE
	TMap<FName, FString> Sounds;  // aware, hit, miss, death -> original sound files
};

/** One bitmap placed for drawing, in draw order. */
struct FMRSpritePlaced
{
	FName Part;
	const FMRSpritePart* PartDef = nullptr;
	int32 Bitmap = INDEX_NONE;
	FVector2f Pos = FVector2f::ZeroVector;  // top-left in base pixels
	float Scale = 1.f;                      // bitmap pixels -> base pixels
	int32 Depth = 0;
};

class MERIDIANREMASTERED_API FMRSpriteLibrary
{
public:
	/** Loaded on first use from the data folder (UMRZoneSubsystem::GetDataDir). */
	static const FMRSpriteLibrary& Get();
	/** Parse from JSON text (tests). */
	bool LoadFromStrings(const FString& PartsJson, const FString& ActionsJson);

	bool IsValid() const { return Bgfs.Num() > 0; }

	TMap<FString, FMRSpriteBgf> Bgfs;
	TMap<FString, FMRSpriteAtlas> Atlases;
	TMap<FName, FMRSpriteLook> Looks;
	TMap<FName, FMRSpriteAction> Actions;
	/** By third-person weapon bgf ("fist" = no weapon). */
	TMap<FString, FMRFirstPersonOverlay> FirstPerson;
	/** Surface classes in index order (the index is written into the code render target). */
	TArray<FMRSpriteMaterialClass> MaterialClasses;
	/** Actions of one look (monsters animate their own way: their Kod's groups and timings). */
	TMap<FName, TMap<FName, FMRSpriteAction>> LookActions;
	/** Monster and NPC classes. */
	TMap<FName, FMRMonsterDef> Monsters;

	/** An action for a look: its own (monsters), else the shared player one. */
	const FMRSpriteAction* FindAction(FName Look, FName Action) const;
	FString TextureDir = TEXT("/Game/Generated/Sprites");
	int32 AtlasScale = 4;          // atlas texels per original pixel
	float SquareCm = 220.f;
	float FinePerSquare = 1024.f;

	const FMRSpriteBgf* FindBgf(const FString& Name) const { return Bgfs.Find(Name); }

	/** cm per base pixel of a torso with this shrink (one pixel = 16/shrink Kod fine units). */
	float CmPerBasePixel(int32 Shrink) const { return 16.f / Shrink / FinePerSquare * SquareCm; }

	/**
	 * Port of D3DRenderOverlaysDraw / FindHotspot: every bitmap of a look at an angle, in draw order
	 * (underlay passes, the body, overlay passes), positioned in base pixels. Groups: part -> 0-based
	 * group. Returns false if the body has no bitmap. OutFeet = where the object stands, base pixels.
	 * bBackArmsUnder: seen from behind (view slots 3-5), the arms (and what they hold) go under the
	 * torso even where the original's torso bitmap puts them over it. Its attack and dance torsos
	 * from behind (bitmap 9 of bta / btb) mark the right arm "over", so a punch or a backswing came
	 * out of the player's back; at the original's size it hardly showed, upscaled it does.
	 */
	bool Place(const FMRSpriteLook& Look, const TMap<FName, int32>& Groups, int32 Angle,
		TArray<FMRSpritePlaced>& Out, FVector2f& OutFeet, int32& OutShrink,
		const TMap<FName, int32>* BitmapOverride = nullptr, bool bBackArmsUnder = true) const;

	/** UE yaws (degrees) -> the client's relative angle (0..4095, 0 = the object faces the viewer). */
	static int32 RelativeAngle(float FacingYawDeg, float YawToViewerDeg);
	/** GetObjectPdib's slot of a group of N bitmaps. */
	static int32 ViewSlot(int32 Angle, int32 N);

private:
	bool ParseParts(const TSharedPtr<class FJsonObject>& Root);
	bool ParseActions(const TSharedPtr<class FJsonObject>& Root);
	static FMRSpriteTrackDef ParseTrack(const TSharedPtr<class FJsonObject>& O);
	static void ParseAction(FName Name, const TSharedPtr<class FJsonObject>& O, FMRSpriteAction& Out);
};
