#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MRNetObject.generated.h"

class UMRSpriteBodyComponent;

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

private:
	uint32 ServerId = 0;
	FName Look;
	FString ObjectName;
	FVector Target = FVector::ZeroVector;
	float StandYaw = 0.f;
	bool bHasTarget = false;
};
