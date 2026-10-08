#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteAppearance.h"
#include "GameFramework/Character.h"
#include "MRNetObject.generated.h"

class UMRBgfSpriteComponent;
class UMRSpriteBodyComponent;
struct FMRBgf;
struct FMRNetAnimation;

/**
 * Another player, a monster or an NPC in the player's room, as a Meridian server reports it
 * (UMRNetWorldSubsystem spawns one per creature in BP_ROOM_CONTENTS / BP_CREATE).
 *
 * Drawn with the same sprite body as everything else. It walks toward the last position the
 * server sent (BP_MOVE) at the sent speed, so collision and floors come from the zone's geometry,
 * and jumps there when it is far off (a teleport). It never blocks the player: the server, not
 * UE, decides who stands where.
 */
UCLASS(NotPlaceable)
class MERIDIANREMASTERED_API AMRNetObject : public ACharacter
{
	GENERATED_BODY()

public:
	AMRNetObject(const FObjectInitializer& ObjectInitializer);

	/** The server's object id and what to draw. */
	void Init(uint32 InId, FName InLook, const FString& InName);
	/** A player's face parts and colours (MRNetLook::AppearanceFromObject). */
	void SetAppearance(const FMRSpriteAppearance& A);
	/**
	 * No sprite of ours: draw the server's own bitmap (UMRBgfSpriteComponent), with its animation
	 * standing and moving (docs/adr/0012). BgfName is the object's icon ("rat.bgf").
	 */
	void SetBgfSprite(const FString& InBgfName, TSharedPtr<const FMRBgf> Bgf);
	void SetServerAnimation(const FMRNetAnimation& Standing, const FMRNetAnimation& Moving);
	/** Play a one-off action on its sprite body (a swing, a monster's bite: the server's ANIMATE_ONCE). */
	void PlayAction(FName Action);
	UMRSpriteBodyComponent* GetSpriteBody() const { return SpriteBody; }
	/**
	 * Not a creature: it never walks (a server move just puts it there), no movement ticks. The
	 * server's items, signs, containers and ornaments.
	 */
	void SetStatic();
	bool IsStatic() const { return bStatic; }
	/**
	 * Drawn by a prop of the world build standing on its square (a lamp, a table, a sign): no sprite
	 * of its own. PropTop is the prop's top (world Z), where its name goes.
	 */
	void SetShownByProp(double PropTop);
	bool IsShownByProp() const { return bShownByProp; }
	/** The original's drawing effect (DRAWFX_*): invisible hides it, black darkens it, translucent fades it. */
	void SetDrawEffect(uint8 Effect);
	uint8 GetDrawEffect() const { return DrawEffect; }
	/** Where its name is drawn: just over the top of what shows (world). */
	FVector GetNameAnchor() const;
	/** The object's flags (OF_*), name colour and minimap flags, as the server last sent them. */
	void SetServerInfo(uint32 InFlags, uint32 InNameColor, uint32 InMinimapFlags) { Flags = InFlags; NameColor = InNameColor; MinimapFlags = InMinimapFlags; }
	uint32 GetFlags() const { return Flags; }
	uint32 GetNameColor() const { return NameColor; }
	uint32 GetMinimapFlags() const { return MinimapFlags; }
	/** The bitmap drawn for an object without a sprite of ours (lower case), else empty. */
	const FString& GetBgfName() const { return BgfName; }
	UMRBgfSpriteComponent* GetBgfSprite() const { return BgfSprite; }

	/** A new target position (world) and the server's speed for it (0: standing). */
	void MoveTo(const FVector& World, uint8 Speed);
	/** Face a Kod angle when standing. */
	void TurnTo(int32 KodAngle);
	/** Jump straight to a position. */
	void Place(const FVector& World, int32 KodAngle);

	uint32 GetServerId() const { return ServerId; }
	const FString& GetObjectName() const { return ObjectName; }
	FName GetLook() const { return Look; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> SpriteBody;

	UPROPERTY(Transient)
	TObjectPtr<UMRBgfSpriteComponent> BgfSprite;

private:
	uint32 ServerId = 0;
	FName Look;
	FString ObjectName;
	FString BgfName;
	bool bStatic = false;
	bool bShownByProp = false;
	double PropTop = 0.0;
	uint8 DrawEffect = 0;
	uint32 Flags = 0;
	uint32 NameColor = 0;
	uint32 MinimapFlags = 0;
	FVector Target = FVector::ZeroVector;
	float StandYaw = 0.f;
	bool bHasTarget = false;
};
