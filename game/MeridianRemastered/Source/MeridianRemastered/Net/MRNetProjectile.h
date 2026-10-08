#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRNetProjectile.generated.h"

class UMRBgfSpriteComponent;
class UPointLightComponent;
struct FMRBgf;
struct FMRNetAnimation;
struct FMRNetLight;

/**
 * Something the server shot across the room (BP_SHOOT, BP_RADIUS_SHOOT; docs/adr/0012 M4): an arrow,
 * a fireball. As the original (clientd3d project.c) it is only a picture: it flies straight from where
 * it starts to where it ends at the server's speed and is gone on arrival; the server has already
 * decided what it does. Drawn from its own bitmap, facing the way it flies, with its light if it has one.
 */
UCLASS(NotPlaceable)
class MERIDIANREMASTERED_API AMRNetProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMRNetProjectile();

	/** Start the flight between two floor points (world), at SpeedCms; bFollowGround keeps it on the floor. */
	void Launch(const FVector& From, const FVector& To, double SpeedCms, bool bFollowGround);
	/** Its picture (the bitmap may arrive after launch) and translation-or-effect (DRAWFX_*, -1 none). */
	void SetBgf(TSharedPtr<const FMRBgf> Bgf, const FMRNetAnimation& Animation, int32 DrawEffect);
	/** The light it gives off (LIGHT_FLAG_*, intensity 0..255, 5:5:5 colour). */
	void SetLight(const FMRNetLight& Light);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMRBgfSpriteComponent> Sprite;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Light;

	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	double Duration = 0.0;
	double Elapsed = 0.0;
	bool bFollowGround = false;
};
