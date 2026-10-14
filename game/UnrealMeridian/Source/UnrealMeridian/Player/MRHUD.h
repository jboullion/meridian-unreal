#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Character/MRSpriteData.h"
#include "MRHUD.generated.h"

/**
 * The in-game HUD. Shows the Slate UI (UMRUISubsystem: vitals, hotbars, minimap, inventory
 * dialog; docs/adr/0009-user-interface.md) for its local player, and draws the original game's first-person hand or weapon (a
 * "window overlay", docs/sprites.md Phase 3) when the player is a sprite body in first person:
 * anchored to the bottom-right corner (HS_SE) and sized like the original D3D client
 * (d3drender.c D3DComputePlayerOverlayArea, as Meridian Shards), bobbing while walking. In the zones we
 * built the hands are quads just in front of the camera, lit by the world like every other sprite
 * (M_BgfSpriteLit): darker in a dim room, as the original and Shards light them by the player's light
 * (mr.Sprite.HandsLit). In rooms built at runtime, whose sprites keep their bitmaps' own brightness, and
 * when blind, they're drawn on the screen.
 *
 * Online the server says what is held (BP_PLAYER_OVERLAY, docs/adr/0012 M2b): each slot's bitmap,
 * screen corner and animation is drawn as the original's DrawPlayerOverlays did; the local swing
 * (the sprite body's) shows in the right hand's place while it plays, ahead of the server's.
 */
UCLASS()
class UNREALMERIDIAN_API AMRHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void DrawHUD() override;
	virtual void PostRender() override;

private:
	float BobPhase = 0.f;
	double LastTime = 0.0;

	/** The server's first-person overlays animating, by slot (PWO_*), and the message each started from. */
	struct FSlotTrack
	{
		uint32 Seq = 0;
		FMRSpriteTrack Track;
	};
	TMap<uint32, FSlotTrack> SlotTracks;
	/** Online, the server's screen effects (BP_EFFECT): on the view (shake, waver, blur, invert) and over it. */
	void DrawScreenEffects(class AMRCharacter* Character, bool bBeforeHands);
	/** The first-person hand or weapon: the server's slots online, the sprite body's offline. */
	void DrawHands(class AMRCharacter* Character, class UMRSpriteBodyComponent* Sprite);
	double WaverPhase = 0.0;
	/** A first-person bitmap placed on the screen (canvas pixels). */
	struct FHand
	{
		class UTexture2D* Tex = nullptr;
		FBox2f UV;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
	};
	/** Where one first-person bitmap goes on the screen, at a hotspot (HS_NW 1 .. HS_CENTER 9). */
	FHand PlaceFirstPerson(class UTexture2D* Tex, const FBox2f& UV, FIntPoint Size, FIntPoint Offset, int32 Hotspot, const FVector2D& Bob, const FVector2D& Scale) const;
	/** The hands as quads hung on the camera (lit by the world), first to last drawn; false if they can't be. */
	bool ShowWorldHands(class AMRCharacter* Character, const TArray<FHand>& Hands);
	/** Hide the world hands from index First on. */
	void HideWorldHands(int32 First = 0);
	/** The character the hands' quads belong to (components of it). */
	TWeakObjectPtr<class AMRCharacter> HandsOwner;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UProceduralMeshComponent>> HandQuads;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> HandMaterials;
};
