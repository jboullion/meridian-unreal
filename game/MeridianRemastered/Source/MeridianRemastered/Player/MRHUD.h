#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Character/MRSpriteData.h"
#include "MRHUD.generated.h"

/**
 * The in-game HUD. Shows the Slate UI (UMRUISubsystem: vitals, hotbars, minimap, inventory
 * dialog; docs/adr/0009-user-interface.md) for its local player, and draws the original game's first-person hand or weapon (a
 * "window overlay", docs/sprites.md Phase 3) when the player is a sprite body in first person:
 * anchored to the bottom-right corner (HS_SE) and scaled like the original client
 * (clientd3d/overlay.c ComputePlayerOverlayArea: viewport width / 452 * 0.5), bobbing while walking.
 *
 * Online the server says what is held (BP_PLAYER_OVERLAY, docs/adr/0012 M2b): each slot's bitmap,
 * screen corner and animation is drawn as the original's DrawPlayerOverlays did; the local swing
 * (the sprite body's) shows in the right hand's place while it plays, ahead of the server's.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void DrawHUD() override;

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
	/** Draw one first-person bitmap at a screen hotspot (HS_NW 1 .. HS_CENTER 9). */
	void DrawFirstPerson(class UTexture2D* Tex, const FBox2f& UV, FIntPoint Size, FIntPoint Offset, int32 Hotspot, const FVector2D& Bob, float Scale);
};
