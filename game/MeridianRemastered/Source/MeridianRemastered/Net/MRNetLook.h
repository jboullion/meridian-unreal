#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteAppearance.h"

struct FMRNetObject;
struct FMRNetOverlay;

/**
 * A player's sprite appearance from what the server sends about it (player.kod SendOverlays): the
 * torso is the object's icon with the shirt's translation; the overlays carry the arms (hotspots
 * 21, 31), legs (41, the pants' translation), head (1), mouth (12), eyes (11), nose (14) and hair
 * (13), the face parts translated with the skin and the hair with its colour. Then come the
 * items' overlays: a weapon (22), shield (32), bow (33), helmet (13, after the hair or in its place).
 * The torso, arms and legs may be armour's, robes' or gauntlets' (docs/adr/0012 M2b).
 */
namespace MRNetLook
{
	/** Female when the head or the torso is the female one (phkx, btb). */
	MERIDIANREMASTERED_API bool IsFemale(const FMRNetObject& Object);

	/**
	 * The appearance to draw a player with: player_male / player_female, the server's face parts
	 * (those the game has converted; the rest keep the look's), skin, hair colour, shirt and pants.
	 * False if the object isn't a player.
	 */
	MERIDIANREMASTERED_API bool AppearanceFromObject(const FMRNetObject& Object, FMRSpriteAppearance& Out);

	/**
	 * Where the items' overlays start in a player's overlay list: after the nose and the hair (the
	 * fixed part of SendOverlays). OutHair: the hair overlay, null when a helmet took it off.
	 */
	MERIDIANREMASTERED_API int32 FirstItemOverlay(const FMRNetObject& Object, const FMRNetOverlay** OutHair = nullptr);
}
