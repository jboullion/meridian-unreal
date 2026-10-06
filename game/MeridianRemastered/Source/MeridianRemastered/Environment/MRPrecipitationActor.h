#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRPrecipitationActor.generated.h"

class UMaterialBillboardComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * What a storm draws around the camera (docs/adr/0005 phase 4), spawned on clients by the
 * environment director. Nothing is simulated on the CPU:
 *  - Falling rain, snow or sand: one static mesh of many tiny quads (SM_Precip, build_prop_kit.py)
 *    that M_Precip moves on the GPU. Every quad becomes a streak or flake wrapping in a box around
 *    the camera, hidden where the zone's shelter map says a roof is above it.
 *  - Splashes: the same quads with M_Splash. Each is a short-lived splash at a random spot near the
 *    camera, on the highest surface there (the shelter map: ground, roofs, the pond).
 *  - A distant lightning bolt: a camera-facing sprite far out in the sky, shown for one stroke.
 *  - Ambient particles (phase 5): the same quads with M_Ambient as dust motes, pollen, fireflies and
 *    leaves, as many as MPC_Environment Motes, Pollen, Fireflies and Leaves say.
 *
 * The actor keeps to a grid of the box size around the camera: the material's wrap is in world
 * space, so moving the actor by whole boxes changes nothing on screen, and the meshes' bounds stay
 * around the camera. The materials read MPC_Environment.Precip (how much falls), Snow, Sand and Wind.
 */
UCLASS(NotPlaceable)
class MERIDIANREMASTERED_API AMRPrecipitationActor : public AActor
{
	GENERATED_BODY()

public:
	AMRPrecipitationActor();

	/** The zone's falling and splash materials (its shelter map); nullptr hides that part. */
	void SetMaterials(UMaterialInterface* Falling, UMaterialInterface* Splash);

	/** What shows: the falling streaks, and the splashes (rain only). Hidden parts cost nothing. */
	void SetFalling(bool bFalling, bool bSplashing);

	/** The zone's ambient particle material (its shelter map), and whether they show. */
	void SetAmbient(UMaterialInterface* Material, bool bShow);

	/** A bolt sprite at a world location (full size in cm), with its material; Hide with nullptr. */
	void ShowBolt(UMaterialInterface* Material, const FVector& Location, float WidthCm, float HeightCm);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The box the streaks wrap in: M_Precip's Box and BoxH must match. */
	static constexpr float BoxCm = 3200.f;
	static constexpr float BoxHeightCm = 1600.f;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Splash;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Ambient;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UMaterialBillboardComponent> Bolt;

	bool bWantFalling = false;
	bool bWantSplashing = false;
	bool bWantAmbient = false;
};
