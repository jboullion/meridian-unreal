#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Character/MRSpriteData.h"
#include "MRSpriteBodyComponent.generated.h"

class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * A character drawn the original game's way (docs/sprites.md): the look's parts (torso, arms,
 * legs, head, face, hair, weapon) composited on their hotspots into a render target, chosen by
 * the angle between the character's facing and the viewer, and animated by the original group
 * ranges (data/sprites/player_actions.json). The component is the quad that shows it: upright
 * and parallel to the screen like the original's (mr.Sprite.Billboard: how much of the camera's
 * pitch it follows), lit like the world.
 *
 * Client only: a dedicated server never creates one. Not replicated yet (Phase 2 is local).
 */
UCLASS(ClassGroup = (Meridian), meta = (BlueprintSpawnableComponent))
class MERIDIANREMASTERED_API UMRSpriteBodyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UMRSpriteBodyComponent();

	/** Choose a look from player_parts.json (e.g. "test_male"). */
	void SetLook(FName LookName);
	FName GetLook() const { return Look ? Look->Name : NAME_None; }

	/** Load a look's atlases ahead of SetLook (a monster's corpse), so it doesn't pop in late. */
	void PrewarmLook(FName LookName);

	/** Play an action from player_actions.json: one-shots (fist_attack, wave...) end by themselves; loops (dance) until StopAction or movement. */
	void PlayAction(FName Action);
	void StopAction();
	FName GetAction() const { return CurrentAction; }

	/**
	 * Colours (the original creator's choices, FMRSpriteColours; -1 keeps the look's own): skin
	 * 0..3, hair 0..13, shirt and pants 0..10. Applied at draw time (runtime palette translation).
	 */
	void SetColours(int32 Skin, int32 Hair, int32 Shirt, int32 Pants);

	/** Height variety (Phase 6): scales the drawing around the feet; collision is unchanged. */
	void SetHeightScale(float InScale);
	float GetHeightScale() const { return HeightScale; }

	/** Sprite height in cm (standing, at this height scale). */
	float GetStandingHeightCm() const;

	/** The first-person hand / weapon to draw now (AMRHUD): false if none. Offset and size in original pixels. */
	bool GetFirstPersonFrame(UTexture2D*& OutTexture, FBox2f& OutUV, FIntPoint& OutSize, FIntPoint& OutOffset);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	const FMRSpriteLook* Look = nullptr;
	float HeightScale = 1.f;

	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
	/** Per pixel the part's palette translation (R) and surface class (G): M_SpriteBody recolours with it. */
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> CodeTarget;
	/** Each part's palette translation now (the look's, or SetColours'). */
	TMap<FName, int32> PartXlat;
	void ApplyLightingParams();
	float LastAlbedo = -1.f, LastNormalUp = -1.f, LastAmbient = -1.f;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	/** Upright copy turned to the sun: casts the shadow (hidden itself). */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ShadowCard;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> Textures;

	TMap<FName, FMRSpriteTrack> BaseTracks;    // stand / walk
	TMap<FName, FMRSpriteTrack> ActionTracks;  // the current action
	FMRSpriteTrack FirstPersonTrack;          // the first-person hand / weapon (Kod groups; 0 = none)
	const FMRFirstPersonOverlay* FirstPersonDef = nullptr;
	FName BaseAction;
	FName CurrentAction;
	bool bActionLoops = false;

	float TexelsPerBasePixel = 4.f;
	int32 LastShrink = 4;
	uint32 LastDrawKey = 0;
	int32 LastAngle = 0;
	bool bUnlit = false;
	float SunYaw = 0.f;
	FVector SunDir = FVector(0.f, 0.f, -1.f);
	float ShadowFeetOffset = 0.f;  // shadow card centre above the feet
	float SunCheckTimer = 0.f;

	void EnsureTarget();
	void ApplyMaterial();
	void SetBaseAction(FName Action);
	void StartTracks(const FMRSpriteAction& Action, TMap<FName, FMRSpriteTrack>& Into);
	TMap<FName, int32> CurrentGroups() const;
	/** The track animating a part now (the action's, else stand / walk), or null. */
	const FMRSpriteTrack* TrackFor(FName Part) const;

	/** One bitmap to draw into the render target (a placed part with its feet and opacity). */
	struct FDrawItem
	{
		const FMRSpritePart* PartDef = nullptr;
		int32 Bitmap = INDEX_NONE;
		FVector2f Pos = FVector2f::ZeroVector;
		float Scale = 1.f;
		FVector2f Feet = FVector2f::ZeroVector;
		float Alpha = 1.f;
	};
	/** Build what to draw this frame (in-betweens, crossfades, angle fade), redraw if it changed. */
	void Compose(int32 Angle, float DeltaTime);
	void DrawItems(const TArray<FDrawItem>& Items);

	// angle fade (mr.Sprite.Smooth.AngleFade): the last frame of the previous view, faded out
	TArray<FMRSpritePlaced> LastPlaced;
	FVector2f LastFeet = FVector2f::ZeroVector;
	int32 LastSlot = INDEX_NONE;
	TArray<FMRSpritePlaced> FadeFrom;
	FVector2f FadeFromFeet = FVector2f::ZeroVector;
	float FadeElapsed = 1e9f;

	// procedural motion on the quad (mr.Sprite.Smooth.Motion): bob, breathing, lean, squash
	void UpdateMotion(float DeltaTime, bool bMoving);
	float MotionTime = 0.f;
	float MotionBobCm = 0.f;
	float MotionScaleZ = 1.f;
	float MotionRollDeg = 0.f;
	float LastYaw = 0.f;
	float SquashTimer = 0.f;
	bool bWasFalling = false;
	UTexture2D* AtlasTexture(const FString& Key);
	static bool IsReady(const UTexture2D* Tex);
	void PlaceQuad(UStaticMeshComponent* Quad, float FaceYaw, float Lean);
	void UpdateSun(float DeltaTime);
};
