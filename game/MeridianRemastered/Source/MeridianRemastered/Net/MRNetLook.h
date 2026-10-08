#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteAppearance.h"

struct FMRNetObject;

/**
 * A player's sprite appearance from what the server sends about it (player.kod SendOverlays): the
 * torso is the object's icon with the shirt's translation; the overlays carry the arms (hotspots
 * 21, 31), legs (41, the pants' translation), head (1), mouth (12), eyes (11), nose (14) and hair
 * (13), the face parts translated with the skin and the hair with its colour.
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
}
